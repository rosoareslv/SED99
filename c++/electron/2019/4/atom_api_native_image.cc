// Copyright (c) 2015 GitHub, Inc.
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "atom/common/api/atom_api_native_image.h"

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "atom/common/asar/asar_util.h"
#include "atom/common/native_mate_converters/file_path_converter.h"
#include "atom/common/native_mate_converters/gfx_converter.h"
#include "atom/common/native_mate_converters/gurl_converter.h"
#include "atom/common/native_mate_converters/value_converter.h"
#include "atom/common/node_includes.h"
#include "base/files/file_util.h"
#include "base/strings/pattern.h"
#include "base/strings/string_util.h"
#include "base/threading/thread_restrictions.h"
#include "native_mate/object_template_builder.h"
#include "net/base/data_url.h"
#include "third_party/skia/include/core/SkBitmap.h"
#include "third_party/skia/include/core/SkImageInfo.h"
#include "third_party/skia/include/core/SkPixelRef.h"
#include "ui/base/layout.h"
#include "ui/base/webui/web_ui_util.h"
#include "ui/gfx/codec/jpeg_codec.h"
#include "ui/gfx/codec/png_codec.h"
#include "ui/gfx/geometry/size.h"
#include "ui/gfx/image/image_skia.h"
#include "ui/gfx/image/image_skia_operations.h"
#include "ui/gfx/image/image_util.h"

#if defined(OS_WIN)
#include "atom/common/asar/archive.h"
#include "base/win/scoped_gdi_object.h"
#include "ui/gfx/icon_util.h"
#endif

namespace atom {

namespace api {

namespace {

struct ScaleFactorPair {
  const char* name;
  float scale;
};

ScaleFactorPair kScaleFactorPairs[] = {
    // The "@2x" is put as first one to make scale matching faster.
    {"@2x", 2.0f},   {"@3x", 3.0f},     {"@1x", 1.0f},     {"@4x", 4.0f},
    {"@5x", 5.0f},   {"@1.25x", 1.25f}, {"@1.33x", 1.33f}, {"@1.4x", 1.4f},
    {"@1.5x", 1.5f}, {"@1.8x", 1.8f},   {"@2.5x", 2.5f},
};

float GetScaleFactorFromPath(const base::FilePath& path) {
  std::string filename(path.BaseName().RemoveExtension().AsUTF8Unsafe());

  // We don't try to convert string to float here because it is very very
  // expensive.
  for (const auto& kScaleFactorPair : kScaleFactorPairs) {
    if (base::EndsWith(filename, kScaleFactorPair.name,
                       base::CompareCase::INSENSITIVE_ASCII))
      return kScaleFactorPair.scale;
  }

  return 1.0f;
}

// Get the scale factor from options object at the first argument
float GetScaleFactorFromOptions(mate::Arguments* args) {
  float scale_factor = 1.0f;
  mate::Dictionary options;
  if (args->GetNext(&options))
    options.Get("scaleFactor", &scale_factor);
  return scale_factor;
}

bool AddImageSkiaRepFromPNG(gfx::ImageSkia* image,
                            const unsigned char* data,
                            size_t size,
                            double scale_factor) {
  SkBitmap bitmap;
  if (!gfx::PNGCodec::Decode(data, size, &bitmap))
    return false;

  image->AddRepresentation(gfx::ImageSkiaRep(bitmap, scale_factor));
  return true;
}

bool AddImageSkiaRepFromJPEG(gfx::ImageSkia* image,
                             const unsigned char* data,
                             size_t size,
                             double scale_factor) {
  auto bitmap = gfx::JPEGCodec::Decode(data, size);
  if (!bitmap)
    return false;

  // `JPEGCodec::Decode()` doesn't tell `SkBitmap` instance it creates
  // that all of its pixels are opaque, that's why the bitmap gets
  // an alpha type `kPremul_SkAlphaType` instead of `kOpaque_SkAlphaType`.
  // Let's fix it here.
  // TODO(alexeykuzmin): This workaround should be removed
  // when the `JPEGCodec::Decode()` code is fixed.
  // See https://github.com/electron/electron/issues/11294.
  bitmap->setAlphaType(SkAlphaType::kOpaque_SkAlphaType);

  image->AddRepresentation(gfx::ImageSkiaRep(*bitmap, scale_factor));
  return true;
}

bool AddImageSkiaRepFromBuffer(gfx::ImageSkia* image,
                               const unsigned char* data,
                               size_t size,
                               int width,
                               int height,
                               double scale_factor) {
  // Try PNG first.
  if (AddImageSkiaRepFromPNG(image, data, size, scale_factor))
    return true;

  // Try JPEG second.
  if (AddImageSkiaRepFromJPEG(image, data, size, scale_factor))
    return true;

  if (width == 0 || height == 0)
    return false;

  auto info = SkImageInfo::MakeN32(width, height, kPremul_SkAlphaType);
  if (size < info.computeMinByteSize())
    return false;

  SkBitmap bitmap;
  bitmap.allocN32Pixels(width, height, false);
  bitmap.writePixels({info, data, bitmap.rowBytes()});

  image->AddRepresentation(gfx::ImageSkiaRep(bitmap, scale_factor));
  return true;
}

bool AddImageSkiaRepFromPath(gfx::ImageSkia* image,
                             const base::FilePath& path,
                             double scale_factor) {
  std::string file_contents;
  {
    base::ThreadRestrictions::ScopedAllowIO allow_io;
    if (!asar::ReadFileToString(path, &file_contents))
      return false;
  }

  const unsigned char* data =
      reinterpret_cast<const unsigned char*>(file_contents.data());
  size_t size = file_contents.size();

  return AddImageSkiaRepFromBuffer(image, data, size, 0, 0, scale_factor);
}

bool PopulateImageSkiaRepsFromPath(gfx::ImageSkia* image,
                                   const base::FilePath& path) {
  bool succeed = false;
  std::string filename(path.BaseName().RemoveExtension().AsUTF8Unsafe());
  if (base::MatchPattern(filename, "*@*x"))
    // Don't search for other representations if the DPI has been specified.
    return AddImageSkiaRepFromPath(image, path, GetScaleFactorFromPath(path));
  else
    succeed |= AddImageSkiaRepFromPath(image, path, 1.0f);

  for (const ScaleFactorPair& pair : kScaleFactorPairs)
    succeed |= AddImageSkiaRepFromPath(
        image, path.InsertBeforeExtensionASCII(pair.name), pair.scale);
  return succeed;
}

base::FilePath NormalizePath(const base::FilePath& path) {
  if (!path.ReferencesParent()) {
    return path;
  }

  base::FilePath absolute_path = MakeAbsoluteFilePath(path);
  // MakeAbsoluteFilePath returns an empty path on failures so use original path
  if (absolute_path.empty()) {
    return path;
  } else {
    return absolute_path;
  }
}

#if defined(OS_MACOSX)
bool IsTemplateFilename(const base::FilePath& path) {
  return (base::MatchPattern(path.value(), "*Template.*") ||
          base::MatchPattern(path.value(), "*Template@*x.*"));
}
#endif

#if defined(OS_WIN)
base::win::ScopedHICON ReadICOFromPath(int size, const base::FilePath& path) {
  // If file is in asar archive, we extract it to a temp file so LoadImage can
  // load it.
  base::FilePath asar_path, relative_path;
  base::FilePath image_path(path);
  if (asar::GetAsarArchivePath(image_path, &asar_path, &relative_path)) {
    std::shared_ptr<asar::Archive> archive =
        asar::GetOrCreateAsarArchive(asar_path);
    if (archive)
      archive->CopyFileOut(relative_path, &image_path);
  }

  // Load the icon from file.
  return base::win::ScopedHICON(
      static_cast<HICON>(LoadImage(NULL, image_path.value().c_str(), IMAGE_ICON,
                                   size, size, LR_LOADFROMFILE)));
}

bool ReadImageSkiaFromICO(gfx::ImageSkia* image, HICON icon) {
  // Convert the icon from the Windows specific HICON to gfx::ImageSkia.
  SkBitmap bitmap = IconUtil::CreateSkBitmapFromHICON(icon);
  if (bitmap.isNull())
    return false;

  image->AddRepresentation(gfx::ImageSkiaRep(bitmap, 1.0f));
  return true;
}
#endif

void Noop(char*, void*) {}

}  // namespace

NativeImage::NativeImage(v8::Isolate* isolate, const gfx::Image& image)
    : image_(image) {
  Init(isolate);
  if (image_.HasRepresentation(gfx::Image::kImageRepSkia)) {
    isolate->AdjustAmountOfExternalAllocatedMemory(
        image_.ToImageSkia()->bitmap()->computeByteSize());
  }
}

#if defined(OS_WIN)
NativeImage::NativeImage(v8::Isolate* isolate, const base::FilePath& hicon_path)
    : hicon_path_(hicon_path) {
  // Use the 256x256 icon as fallback icon.
  gfx::ImageSkia image_skia;
  ReadImageSkiaFromICO(&image_skia, GetHICON(256));
  image_ = gfx::Image(image_skia);
  Init(isolate);
  if (image_.HasRepresentation(gfx::Image::kImageRepSkia)) {
    isolate->AdjustAmountOfExternalAllocatedMemory(
        image_.ToImageSkia()->bitmap()->computeByteSize());
  }
}
#endif

NativeImage::~NativeImage() {
  if (image_.HasRepresentation(gfx::Image::kImageRepSkia)) {
    isolate()->AdjustAmountOfExternalAllocatedMemory(-static_cast<int64_t>(
        image_.ToImageSkia()->bitmap()->computeByteSize()));
  }
}

#if defined(OS_WIN)
HICON NativeImage::GetHICON(int size) {
  auto iter = hicons_.find(size);
  if (iter != hicons_.end())
    return iter->second.get();

  // First try loading the icon with specified size.
  if (!hicon_path_.empty()) {
    hicons_[size] = ReadICOFromPath(size, hicon_path_);
    return hicons_[size].get();
  }

  // Then convert the image to ICO.
  if (image_.IsEmpty())
    return NULL;
  hicons_[size] = IconUtil::CreateHICONFromSkBitmap(image_.AsBitmap());
  return hicons_[size].get();
}
#endif

v8::Local<v8::Value> NativeImage::ToPNG(mate::Arguments* args) {
  float scale_factor = GetScaleFactorFromOptions(args);

  if (scale_factor == 1.0f) {
    // Use raw 1x PNG bytes when available
    scoped_refptr<base::RefCountedMemory> png = image_.As1xPNGBytes();
    if (png->size() > 0) {
      const char* data = reinterpret_cast<const char*>(png->front());
      size_t size = png->size();
      return node::Buffer::Copy(args->isolate(), data, size).ToLocalChecked();
    }
  }

  const SkBitmap bitmap =
      image_.AsImageSkia().GetRepresentation(scale_factor).GetBitmap();
  std::vector<unsigned char> encoded;
  gfx::PNGCodec::EncodeBGRASkBitmap(bitmap, false, &encoded);
  const char* data = reinterpret_cast<char*>(encoded.data());
  size_t size = encoded.size();
  return node::Buffer::Copy(args->isolate(), data, size).ToLocalChecked();
}

v8::Local<v8::Value> NativeImage::ToBitmap(mate::Arguments* args) {
  float scale_factor = GetScaleFactorFromOptions(args);

  const SkBitmap bitmap =
      image_.AsImageSkia().GetRepresentation(scale_factor).GetBitmap();
  SkPixelRef* ref = bitmap.pixelRef();
  if (!ref)
    return node::Buffer::New(args->isolate(), 0).ToLocalChecked();
  return node::Buffer::Copy(args->isolate(),
                            reinterpret_cast<const char*>(ref->pixels()),
                            bitmap.computeByteSize())
      .ToLocalChecked();
}

v8::Local<v8::Value> NativeImage::ToJPEG(v8::Isolate* isolate, int quality) {
  std::vector<unsigned char> output;
  gfx::JPEG1xEncodedDataFromImage(image_, quality, &output);
  if (output.empty())
    return node::Buffer::New(isolate, 0).ToLocalChecked();
  return node::Buffer::Copy(isolate,
                            reinterpret_cast<const char*>(&output.front()),
                            output.size())
      .ToLocalChecked();
}

std::string NativeImage::ToDataURL(mate::Arguments* args) {
  float scale_factor = GetScaleFactorFromOptions(args);

  if (scale_factor == 1.0f) {
    // Use raw 1x PNG bytes when available
    scoped_refptr<base::RefCountedMemory> png = image_.As1xPNGBytes();
    if (png->size() > 0)
      return webui::GetPngDataUrl(png->front(), png->size());
  }

  return webui::GetBitmapDataUrl(
      image_.AsImageSkia().GetRepresentation(scale_factor).GetBitmap());
}

v8::Local<v8::Value> NativeImage::GetBitmap(mate::Arguments* args) {
  float scale_factor = GetScaleFactorFromOptions(args);

  const SkBitmap bitmap =
      image_.AsImageSkia().GetRepresentation(scale_factor).GetBitmap();
  SkPixelRef* ref = bitmap.pixelRef();
  if (!ref)
    return node::Buffer::New(args->isolate(), 0).ToLocalChecked();
  return node::Buffer::New(args->isolate(),
                           reinterpret_cast<char*>(ref->pixels()),
                           bitmap.computeByteSize(), &Noop, nullptr)
      .ToLocalChecked();
}

v8::Local<v8::Value> NativeImage::GetNativeHandle(v8::Isolate* isolate,
                                                  mate::Arguments* args) {
#if defined(OS_MACOSX)
  if (IsEmpty())
    return node::Buffer::New(isolate, 0).ToLocalChecked();

  NSImage* ptr = image_.AsNSImage();
  return node::Buffer::Copy(isolate, reinterpret_cast<char*>(ptr),
                            sizeof(void*))
      .ToLocalChecked();
#else
  args->ThrowError("Not implemented");
  return v8::Undefined(isolate);
#endif
}

bool NativeImage::IsEmpty() {
  return image_.IsEmpty();
}

gfx::Size NativeImage::GetSize() {
  return image_.Size();
}

float NativeImage::GetAspectRatio() {
  gfx::Size size = GetSize();
  if (size.IsEmpty())
    return 1.f;
  else
    return static_cast<float>(size.width()) / static_cast<float>(size.height());
}

mate::Handle<NativeImage> NativeImage::Resize(
    v8::Isolate* isolate,
    const base::DictionaryValue& options) {
  gfx::Size size = GetSize();
  int width = size.width();
  int height = size.height();
  bool width_set = options.GetInteger("width", &width);
  bool height_set = options.GetInteger("height", &height);
  size.SetSize(width, height);

  if (width_set && !height_set) {
    // Scale height to preserve original aspect ratio
    size.set_height(width);
    size = gfx::ScaleToRoundedSize(size, 1.f, 1.f / GetAspectRatio());
  } else if (height_set && !width_set) {
    // Scale width to preserve original aspect ratio
    size.set_width(height);
    size = gfx::ScaleToRoundedSize(size, GetAspectRatio(), 1.f);
  }

  skia::ImageOperations::ResizeMethod method =
      skia::ImageOperations::ResizeMethod::RESIZE_BEST;
  std::string quality;
  options.GetString("quality", &quality);
  if (quality == "good")
    method = skia::ImageOperations::ResizeMethod::RESIZE_GOOD;
  else if (quality == "better")
    method = skia::ImageOperations::ResizeMethod::RESIZE_BETTER;

  gfx::ImageSkia resized = gfx::ImageSkiaOperations::CreateResizedImage(
      image_.AsImageSkia(), method, size);
  return mate::CreateHandle(isolate,
                            new NativeImage(isolate, gfx::Image(resized)));
}

mate::Handle<NativeImage> NativeImage::Crop(v8::Isolate* isolate,
                                            const gfx::Rect& rect) {
  gfx::ImageSkia cropped =
      gfx::ImageSkiaOperations::ExtractSubset(image_.AsImageSkia(), rect);
  return mate::CreateHandle(isolate,
                            new NativeImage(isolate, gfx::Image(cropped)));
}

void NativeImage::AddRepresentation(const mate::Dictionary& options) {
  int width = 0;
  int height = 0;
  float scale_factor = 1.0f;
  options.Get("width", &width);
  options.Get("height", &height);
  options.Get("scaleFactor", &scale_factor);

  bool skia_rep_added = false;
  gfx::ImageSkia image_skia = image_.AsImageSkia();

  v8::Local<v8::Value> buffer;
  GURL url;
  if (options.Get("buffer", &buffer) && node::Buffer::HasInstance(buffer)) {
    auto* data = reinterpret_cast<unsigned char*>(node::Buffer::Data(buffer));
    auto size = node::Buffer::Length(buffer);
    skia_rep_added = AddImageSkiaRepFromBuffer(&image_skia, data, size, width,
                                               height, scale_factor);
  } else if (options.Get("dataURL", &url)) {
    std::string mime_type, charset, data;
    if (net::DataURL::Parse(url, &mime_type, &charset, &data)) {
      auto* data_ptr = reinterpret_cast<const unsigned char*>(data.c_str());
      if (mime_type == "image/png") {
        skia_rep_added = AddImageSkiaRepFromPNG(&image_skia, data_ptr,
                                                data.size(), scale_factor);
      } else if (mime_type == "image/jpeg") {
        skia_rep_added = AddImageSkiaRepFromJPEG(&image_skia, data_ptr,
                                                 data.size(), scale_factor);
      }
    }
  }

  // Re-initialize image when first representation is added to an empty image
  if (skia_rep_added && IsEmpty()) {
    gfx::Image image(image_skia);
    image_ = std::move(image);
  }
}

#if !defined(OS_MACOSX)
void NativeImage::SetTemplateImage(bool setAsTemplate) {}

bool NativeImage::IsTemplateImage() {
  return false;
}
#endif

// static
mate::Handle<NativeImage> NativeImage::CreateEmpty(v8::Isolate* isolate) {
  return mate::CreateHandle(isolate, new NativeImage(isolate, gfx::Image()));
}

// static
mate::Handle<NativeImage> NativeImage::Create(v8::Isolate* isolate,
                                              const gfx::Image& image) {
  return mate::CreateHandle(isolate, new NativeImage(isolate, image));
}

// static
mate::Handle<NativeImage> NativeImage::CreateFromPNG(v8::Isolate* isolate,
                                                     const char* buffer,
                                                     size_t length) {
  gfx::Image image = gfx::Image::CreateFrom1xPNGBytes(
      reinterpret_cast<const unsigned char*>(buffer), length);
  return Create(isolate, image);
}

// static
mate::Handle<NativeImage> NativeImage::CreateFromJPEG(v8::Isolate* isolate,
                                                      const char* buffer,
                                                      size_t length) {
  gfx::Image image = gfx::ImageFrom1xJPEGEncodedData(
      reinterpret_cast<const unsigned char*>(buffer), length);
  return Create(isolate, image);
}

// static
mate::Handle<NativeImage> NativeImage::CreateFromPath(
    v8::Isolate* isolate,
    const base::FilePath& path) {
  base::FilePath image_path = NormalizePath(path);
#if defined(OS_WIN)
  if (image_path.MatchesExtension(FILE_PATH_LITERAL(".ico"))) {
    return mate::CreateHandle(isolate, new NativeImage(isolate, image_path));
  }
#endif
  gfx::ImageSkia image_skia;
  PopulateImageSkiaRepsFromPath(&image_skia, image_path);
  gfx::Image image(image_skia);
  mate::Handle<NativeImage> handle = Create(isolate, image);
#if defined(OS_MACOSX)
  if (IsTemplateFilename(image_path))
    handle->SetTemplateImage(true);
#endif
  return handle;
}

// static
mate::Handle<NativeImage> NativeImage::CreateFromBitmap(
    mate::Arguments* args,
    v8::Local<v8::Value> buffer,
    const mate::Dictionary& options) {
  if (!node::Buffer::HasInstance(buffer)) {
    args->ThrowError("buffer must be a node Buffer");
    return mate::Handle<NativeImage>();
  }

  unsigned int width = 0;
  unsigned int height = 0;
  double scale_factor = 1.;

  if (!options.Get("width", &width)) {
    args->ThrowError("width is required");
    return mate::Handle<NativeImage>();
  }

  if (!options.Get("height", &height)) {
    args->ThrowError("height is required");
    return mate::Handle<NativeImage>();
  }

  auto info = SkImageInfo::MakeN32(width, height, kPremul_SkAlphaType);
  auto size_bytes = info.computeMinByteSize();

  if (size_bytes != node::Buffer::Length(buffer)) {
    args->ThrowError("invalid buffer size");
    return mate::Handle<NativeImage>();
  }

  options.Get("scaleFactor", &scale_factor);

  if (width == 0 || height == 0) {
    return CreateEmpty(args->isolate());
  }

  SkBitmap bitmap;
  bitmap.allocN32Pixels(width, height, false);
  bitmap.writePixels({info, node::Buffer::Data(buffer), bitmap.rowBytes()});

  gfx::ImageSkia image_skia;
  image_skia.AddRepresentation(gfx::ImageSkiaRep(bitmap, scale_factor));

  return Create(args->isolate(), gfx::Image(image_skia));
}

// static
mate::Handle<NativeImage> NativeImage::CreateFromBuffer(
    mate::Arguments* args,
    v8::Local<v8::Value> buffer) {
  if (!node::Buffer::HasInstance(buffer)) {
    args->ThrowError("buffer must be a node Buffer");
    return mate::Handle<NativeImage>();
  }

  int width = 0;
  int height = 0;
  double scale_factor = 1.;

  mate::Dictionary options;
  if (args->GetNext(&options)) {
    options.Get("width", &width);
    options.Get("height", &height);
    options.Get("scaleFactor", &scale_factor);
  }

  gfx::ImageSkia image_skia;
  AddImageSkiaRepFromBuffer(
      &image_skia, reinterpret_cast<unsigned char*>(node::Buffer::Data(buffer)),
      node::Buffer::Length(buffer), width, height, scale_factor);
  return Create(args->isolate(), gfx::Image(image_skia));
}

// static
mate::Handle<NativeImage> NativeImage::CreateFromDataURL(v8::Isolate* isolate,
                                                         const GURL& url) {
  std::string mime_type, charset, data;
  if (net::DataURL::Parse(url, &mime_type, &charset, &data)) {
    if (mime_type == "image/png")
      return CreateFromPNG(isolate, data.c_str(), data.size());
    else if (mime_type == "image/jpeg")
      return CreateFromJPEG(isolate, data.c_str(), data.size());
  }

  return CreateEmpty(isolate);
}

#if !defined(OS_MACOSX)
mate::Handle<NativeImage> NativeImage::CreateFromNamedImage(
    mate::Arguments* args,
    const std::string& name) {
  return CreateEmpty(args->isolate());
}
#endif

// static
void NativeImage::BuildPrototype(v8::Isolate* isolate,
                                 v8::Local<v8::FunctionTemplate> prototype) {
  prototype->SetClassName(mate::StringToV8(isolate, "NativeImage"));
  mate::ObjectTemplateBuilder(isolate, prototype->PrototypeTemplate())
      .SetMethod("toPNG", &NativeImage::ToPNG)
      .SetMethod("toJPEG", &NativeImage::ToJPEG)
      .SetMethod("toBitmap", &NativeImage::ToBitmap)
      .SetMethod("getBitmap", &NativeImage::GetBitmap)
      .SetMethod("getNativeHandle", &NativeImage::GetNativeHandle)
      .SetMethod("toDataURL", &NativeImage::ToDataURL)
      .SetMethod("isEmpty", &NativeImage::IsEmpty)
      .SetMethod("getSize", &NativeImage::GetSize)
      .SetMethod("setTemplateImage", &NativeImage::SetTemplateImage)
      .SetMethod("isTemplateImage", &NativeImage::IsTemplateImage)
      .SetMethod("resize", &NativeImage::Resize)
      .SetMethod("crop", &NativeImage::Crop)
      .SetMethod("getAspectRatio", &NativeImage::GetAspectRatio)
      .SetMethod("addRepresentation", &NativeImage::AddRepresentation);
}

}  // namespace api

}  // namespace atom

namespace mate {

v8::Local<v8::Value> Converter<mate::Handle<atom::api::NativeImage>>::ToV8(
    v8::Isolate* isolate,
    const mate::Handle<atom::api::NativeImage>& val) {
  return val.ToV8();
}

bool Converter<mate::Handle<atom::api::NativeImage>>::FromV8(
    v8::Isolate* isolate,
    v8::Local<v8::Value> val,
    mate::Handle<atom::api::NativeImage>* out) {
  // Try converting from file path.
  base::FilePath path;
  if (ConvertFromV8(isolate, val, &path)) {
    *out = atom::api::NativeImage::CreateFromPath(isolate, path);
    // Should throw when failed to initialize from path.
    return !(*out)->image().IsEmpty();
  }

  WrappableBase* wrapper =
      static_cast<WrappableBase*>(internal::FromV8Impl(isolate, val));
  if (!wrapper)
    return false;

  *out = CreateHandle(isolate, static_cast<atom::api::NativeImage*>(wrapper));
  return true;
}

}  // namespace mate

namespace {

using atom::api::NativeImage;

void Initialize(v8::Local<v8::Object> exports,
                v8::Local<v8::Value> unused,
                v8::Local<v8::Context> context,
                void* priv) {
  v8::Isolate* isolate = context->GetIsolate();
  mate::Dictionary dict(isolate, exports);
  dict.Set("NativeImage", NativeImage::GetConstructor(isolate)
                              ->GetFunction(context)
                              .ToLocalChecked());
  mate::Dictionary native_image = mate::Dictionary::CreateEmpty(isolate);
  dict.Set("nativeImage", native_image);

  native_image.SetMethod("createEmpty", &NativeImage::CreateEmpty);
  native_image.SetMethod("createFromPath", &NativeImage::CreateFromPath);
  native_image.SetMethod("createFromBitmap", &NativeImage::CreateFromBitmap);
  native_image.SetMethod("createFromBuffer", &NativeImage::CreateFromBuffer);
  native_image.SetMethod("createFromDataURL", &NativeImage::CreateFromDataURL);
  native_image.SetMethod("createFromNamedImage",
                         &NativeImage::CreateFromNamedImage);
}

}  // namespace

NODE_LINKED_MODULE_CONTEXT_AWARE(atom_common_native_image, Initialize)
