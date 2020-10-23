// Copyright (c) 2015 GitHub, Inc.
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "atom/common/api/atom_api_native_image.h"

#include <string>
#include <vector>

#include "atom/common/asar/asar_util.h"
#include "atom/common/native_mate_converters/file_path_converter.h"
#include "atom/common/native_mate_converters/gfx_converter.h"
#include "atom/common/native_mate_converters/gurl_converter.h"
#include "atom/common/node_includes.h"
#include "base/base64.h"
#include "base/files/file_util.h"
#include "base/strings/string_util.h"
#include "base/strings/pattern.h"
#include "native_mate/dictionary.h"
#include "native_mate/object_template_builder.h"
#include "net/base/data_url.h"
#include "ui/base/layout.h"
#include "ui/gfx/codec/jpeg_codec.h"
#include "ui/gfx/codec/png_codec.h"
#include "ui/gfx/geometry/size.h"
#include "ui/gfx/image/image_skia.h"
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
  { "@2x"    , 2.0f },
  { "@3x"    , 3.0f },
  { "@1x"    , 1.0f },
  { "@4x"    , 4.0f },
  { "@5x"    , 5.0f },
  { "@1.25x" , 1.25f },
  { "@1.33x" , 1.33f },
  { "@1.4x"  , 1.4f },
  { "@1.5x"  , 1.5f },
  { "@1.8x"  , 1.8f },
  { "@2.5x"  , 2.5f },
};

float GetScaleFactorFromPath(const base::FilePath& path) {
  std::string filename(path.BaseName().RemoveExtension().AsUTF8Unsafe());

  // We don't try to convert string to float here because it is very very
  // expensive.
  for (unsigned i = 0; i < arraysize(kScaleFactorPairs); ++i) {
    if (base::EndsWith(filename, kScaleFactorPairs[i].name,
                       base::CompareCase::INSENSITIVE_ASCII))
      return kScaleFactorPairs[i].scale;
  }

  return 1.0f;
}

bool AddImageSkiaRep(gfx::ImageSkia* image,
                     const unsigned char* data,
                     size_t size,
                     double scale_factor) {
  scoped_ptr<SkBitmap> decoded(new SkBitmap());

  // Try PNG first.
  if (!gfx::PNGCodec::Decode(data, size, decoded.get()))
    // Try JPEG.
    decoded.reset(gfx::JPEGCodec::Decode(data, size));

  if (!decoded)
    return false;

  image->AddRepresentation(gfx::ImageSkiaRep(*decoded, scale_factor));
  return true;
}

bool AddImageSkiaRep(gfx::ImageSkia* image,
                     const base::FilePath& path,
                     double scale_factor) {
  std::string file_contents;
  if (!asar::ReadFileToString(path, &file_contents))
    return false;

  const unsigned char* data =
      reinterpret_cast<const unsigned char*>(file_contents.data());
  size_t size = file_contents.size();
  return AddImageSkiaRep(image, data, size, scale_factor);
}

bool PopulateImageSkiaRepsFromPath(gfx::ImageSkia* image,
                                   const base::FilePath& path) {
  bool succeed = false;
  std::string filename(path.BaseName().RemoveExtension().AsUTF8Unsafe());
  if (base::MatchPattern(filename, "*@*x"))
    // Don't search for other representations if the DPI has been specified.
    return AddImageSkiaRep(image, path, GetScaleFactorFromPath(path));
  else
    succeed |= AddImageSkiaRep(image, path, 1.0f);

  for (const ScaleFactorPair& pair : kScaleFactorPairs)
    succeed |= AddImageSkiaRep(image,
                               path.InsertBeforeExtensionASCII(pair.name),
                               pair.scale);
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
bool ReadImageSkiaFromICO(gfx::ImageSkia* image, const base::FilePath& path) {
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
  base::win::ScopedHICON icon(static_cast<HICON>(
      LoadImage(NULL, image_path.value().c_str(), IMAGE_ICON, 0, 0,
                LR_DEFAULTSIZE | LR_LOADFROMFILE)));
  if (!icon.get())
    return false;

  // Convert the icon from the Windows specific HICON to gfx::ImageSkia.
  scoped_ptr<SkBitmap> bitmap(IconUtil::  CreateSkBitmapFromHICON(icon.get()));
  image->AddRepresentation(gfx::ImageSkiaRep(*bitmap, 1.0f));
  return true;
}
#endif

}  // namespace

NativeImage::NativeImage(v8::Isolate* isolate, const gfx::Image& image)
    : image_(image) {
  Init(isolate);
}

NativeImage::~NativeImage() {}

v8::Local<v8::Value> NativeImage::ToPNG(v8::Isolate* isolate) {
  scoped_refptr<base::RefCountedMemory> png = image_.As1xPNGBytes();
  return node::Buffer::Copy(isolate,
                            reinterpret_cast<const char*>(png->front()),
                            static_cast<size_t>(png->size())).ToLocalChecked();
}

v8::Local<v8::Value> NativeImage::ToJPEG(v8::Isolate* isolate, int quality) {
  std::vector<unsigned char> output;
  gfx::JPEG1xEncodedDataFromImage(image_, quality, &output);
  return node::Buffer::Copy(
      isolate,
      reinterpret_cast<const char*>(&output.front()),
      static_cast<size_t>(output.size())).ToLocalChecked();
}

std::string NativeImage::ToDataURL() {
  scoped_refptr<base::RefCountedMemory> png = image_.As1xPNGBytes();
  std::string data_url;
  data_url.insert(data_url.end(), png->front(), png->front() + png->size());
  base::Base64Encode(data_url, &data_url);
  data_url.insert(0, "data:image/png;base64,");
  return data_url;
}

v8::Local<v8::Value> NativeImage::GetNativeHandle(v8::Isolate* isolate,
                                                  mate::Arguments* args) {
#if defined(OS_MACOSX)
  NSImage* ptr = image_.AsNSImage();
  return node::Buffer::Copy(
      isolate,
      reinterpret_cast<char*>(ptr),
      sizeof(void*)).ToLocalChecked();
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

#if !defined(OS_MACOSX)
void NativeImage::SetTemplateImage(bool setAsTemplate) {
}

bool NativeImage::IsTemplateImage() {
  return false;
}
#endif

// static
mate::Handle<NativeImage> NativeImage::CreateEmpty(v8::Isolate* isolate) {
  return mate::CreateHandle(isolate, new NativeImage(isolate, gfx::Image()));
}

// static
mate::Handle<NativeImage> NativeImage::Create(
    v8::Isolate* isolate, const gfx::Image& image) {
  return mate::CreateHandle(isolate, new NativeImage(isolate, image));
}

// static
mate::Handle<NativeImage> NativeImage::CreateFromPNG(
    v8::Isolate* isolate, const char* buffer, size_t length) {
  gfx::Image image = gfx::Image::CreateFrom1xPNGBytes(
      reinterpret_cast<const unsigned char*>(buffer), length);
  return Create(isolate, image);
}

// static
mate::Handle<NativeImage> NativeImage::CreateFromJPEG(
    v8::Isolate* isolate, const char* buffer, size_t length) {
  gfx::Image image = gfx::ImageFrom1xJPEGEncodedData(
      reinterpret_cast<const unsigned char*>(buffer), length);
  return Create(isolate, image);
}

// static
mate::Handle<NativeImage> NativeImage::CreateFromPath(
    v8::Isolate* isolate, const base::FilePath& path) {
  gfx::ImageSkia image_skia;
  base::FilePath image_path = NormalizePath(path);

  if (image_path.MatchesExtension(FILE_PATH_LITERAL(".ico"))) {
#if defined(OS_WIN)
    ReadImageSkiaFromICO(&image_skia, image_path);
#endif
  } else {
    PopulateImageSkiaRepsFromPath(&image_skia, image_path);
  }
  gfx::Image image(image_skia);
  mate::Handle<NativeImage> handle = Create(isolate, image);
#if defined(OS_MACOSX)
  if (IsTemplateFilename(image_path))
    handle->SetTemplateImage(true);
#endif
  return handle;
}

// static
mate::Handle<NativeImage> NativeImage::CreateFromBuffer(
    mate::Arguments* args, v8::Local<v8::Value> buffer) {
  double scale_factor = 1.;
  args->GetNext(&scale_factor);

  gfx::ImageSkia image_skia;
  AddImageSkiaRep(&image_skia,
                  reinterpret_cast<unsigned char*>(node::Buffer::Data(buffer)),
                  node::Buffer::Length(buffer),
                  scale_factor);
  return Create(args->isolate(), gfx::Image(image_skia));
}

// static
mate::Handle<NativeImage> NativeImage::CreateFromDataURL(
    v8::Isolate* isolate, const GURL& url) {
  std::string mime_type, charset, data;
  if (net::DataURL::Parse(url, &mime_type, &charset, &data)) {
    if (mime_type == "image/png")
      return CreateFromPNG(isolate, data.c_str(), data.size());
    else if (mime_type == "image/jpeg")
      return CreateFromJPEG(isolate, data.c_str(), data.size());
  }

  return CreateEmpty(isolate);
}

// static
void NativeImage::BuildPrototype(
    v8::Isolate* isolate, v8::Local<v8::ObjectTemplate> prototype) {
  mate::ObjectTemplateBuilder(isolate, prototype)
      .SetMethod("toPng", &NativeImage::ToPNG)
      .SetMethod("toJpeg", &NativeImage::ToJPEG)
      .SetMethod("getNativeHandle", &NativeImage::GetNativeHandle)
      .SetMethod("toDataURL", &NativeImage::ToDataURL)
      .SetMethod("toDataUrl", &NativeImage::ToDataURL)  // deprecated.
      .SetMethod("isEmpty", &NativeImage::IsEmpty)
      .SetMethod("getSize", &NativeImage::GetSize)
      .SetMethod("setTemplateImage", &NativeImage::SetTemplateImage)
      .SetMethod("isTemplateImage", &NativeImage::IsTemplateImage);
}

}  // namespace api

}  // namespace atom


namespace {

void Initialize(v8::Local<v8::Object> exports, v8::Local<v8::Value> unused,
                v8::Local<v8::Context> context, void* priv) {
  mate::Dictionary dict(context->GetIsolate(), exports);
  dict.SetMethod("createEmpty", &atom::api::NativeImage::CreateEmpty);
  dict.SetMethod("createFromPath", &atom::api::NativeImage::CreateFromPath);
  dict.SetMethod("createFromBuffer", &atom::api::NativeImage::CreateFromBuffer);
  dict.SetMethod("createFromDataURL",
                 &atom::api::NativeImage::CreateFromDataURL);
}

}  // namespace

NODE_MODULE_CONTEXT_AWARE_BUILTIN(atom_common_native_image, Initialize)
