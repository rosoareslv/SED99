// Copyright (c) 2015 GitHub, Inc.
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "atom/common/native_mate_converters/net_converter.h"

#include <string>
#include <vector>

#include "atom/common/node_includes.h"
#include "atom/common/native_mate_converters/gurl_converter.h"
#include "atom/common/native_mate_converters/value_converter.h"
#include "base/values.h"
#include "native_mate/dictionary.h"
#include "net/base/upload_bytes_element_reader.h"
#include "net/base/upload_data_stream.h"
#include "net/base/upload_element_reader.h"
#include "net/base/upload_file_element_reader.h"
#include "net/cert/x509_certificate.h"
#include "net/http/http_response_headers.h"
#include "net/url_request/url_request.h"

namespace mate {

// static
v8::Local<v8::Value> Converter<const net::URLRequest*>::ToV8(
    v8::Isolate* isolate, const net::URLRequest* val) {
  scoped_ptr<base::DictionaryValue> dict(new base::DictionaryValue);
  dict->SetString("method", val->method());
  std::string url;
  if (!val->url_chain().empty()) url = val->url().spec();
  dict->SetStringWithoutPathExpansion("url", url);
  dict->SetString("referrer", val->referrer());
  scoped_ptr<base::ListValue> list(new base::ListValue);
  atom::GetUploadData(list.get(), val);
  if (!list->empty())
    dict->Set("uploadData", std::move(list));
  return mate::ConvertToV8(isolate, *(dict.get()));
}

// static
v8::Local<v8::Value> Converter<const net::AuthChallengeInfo*>::ToV8(
    v8::Isolate* isolate, const net::AuthChallengeInfo* val) {
  mate::Dictionary dict = mate::Dictionary::CreateEmpty(isolate);
  dict.Set("isProxy", val->is_proxy);
  dict.Set("scheme", val->scheme);
  dict.Set("host", val->challenger.host());
  dict.Set("port", static_cast<uint32_t>(val->challenger.port()));
  dict.Set("realm", val->realm);
  return mate::ConvertToV8(isolate, dict);
}

// static
v8::Local<v8::Value> Converter<scoped_refptr<net::X509Certificate>>::ToV8(
    v8::Isolate* isolate, const scoped_refptr<net::X509Certificate>& val) {
  mate::Dictionary dict(isolate, v8::Object::New(isolate));
  std::string encoded_data;
  net::X509Certificate::GetPEMEncoded(
      val->os_cert_handle(), &encoded_data);
  auto buffer = node::Buffer::Copy(isolate,
                                   encoded_data.data(),
                                   encoded_data.size()).ToLocalChecked();
  dict.Set("data", buffer);
  dict.Set("issuerName", val->issuer().GetDisplayName());
  return dict.GetHandle();
}

}  // namespace mate

namespace atom {

void GetUploadData(base::ListValue* upload_data_list,
                   const net::URLRequest* request) {
  const net::UploadDataStream* upload_data = request->get_upload();
  if (!upload_data)
    return;
  const std::vector<scoped_ptr<net::UploadElementReader>>* readers =
      upload_data->GetElementReaders();
  for (const auto& reader : *readers) {
    scoped_ptr<base::DictionaryValue> upload_data_dict(
        new base::DictionaryValue);
    if (reader->AsBytesReader()) {
      const net::UploadBytesElementReader* bytes_reader =
          reader->AsBytesReader();
      scoped_ptr<base::Value> bytes(
          base::BinaryValue::CreateWithCopiedBuffer(bytes_reader->bytes(),
                                                    bytes_reader->length()));
      upload_data_dict->Set("bytes", std::move(bytes));
    } else if (reader->AsFileReader()) {
      const net::UploadFileElementReader* file_reader =
          reader->AsFileReader();
      auto file_path = file_reader->path().AsUTF8Unsafe();
      upload_data_dict->SetStringWithoutPathExpansion("file", file_path);
    }
    upload_data_list->Append(std::move(upload_data_dict));
  }
}

}  // namespace atom
