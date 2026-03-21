/**
 * @file http_client_session_attribute.h
 * @brief Attribute wrapper for HttpClientSession.
 * @author Haoming Bai <haomingbai@hotmail.com>
 * @date   2026-03-13
 *
 * Copyright (c) 2026 Haoming Bai
 * SPDX-License-Identifier: MIT
 */

#pragma once

#ifndef BSRVCORE_HTTP_CLIENT_SESSION_ATTRIBUTE_H_
#define BSRVCORE_HTTP_CLIENT_SESSION_ATTRIBUTE_H_

#include <memory>
#include <string>
#include <string_view>

#include "bsrvcore/attribute.h"
#include "bsrvcore/context.h"
#include "bsrvcore/http_client_session.h"

namespace bsrvcore {

/**
 * @brief Attribute that stores a shared HttpClientSession.
 */
class HttpClientSessionAttribute
    : public CloneableAttribute<HttpClientSessionAttribute> {
 public:
  HttpClientSessionAttribute() = default;
  explicit HttpClientSessionAttribute(
      std::shared_ptr<HttpClientSession> session)
      : session_(std::move(session)) {}

  std::shared_ptr<HttpClientSession> Get() const noexcept { return session_; }
  void Set(std::shared_ptr<HttpClientSession> session) {
    session_ = std::move(session);
  }

  std::string ToString() const override {
    return session_ ? "HttpClientSessionAttribute(shared)"
                    : "HttpClientSessionAttribute(null)";
  }

  bool Equals(const Attribute& another) const noexcept override {
    auto* other = dynamic_cast<const HttpClientSessionAttribute*>(&another);
    if (!other) {
      return false;
    }
    return session_ == other->session_;
  }

 private:
  std::shared_ptr<HttpClientSession> session_;
};

/**
 * @brief Default key used when storing client sessions in Context.
 */
inline constexpr std::string_view kHttpClientSessionAttributeKey =
    "bsrvcore.http_client.session";

/**
 * @brief Get an HttpClientSession from Context, or nullptr if missing.
 */
inline std::shared_ptr<HttpClientSession> GetHttpClientSession(
    const std::shared_ptr<Context>& ctx,
    std::string key = std::string(kHttpClientSessionAttributeKey)) {
  if (!ctx) {
    return nullptr;
  }
  auto base = ctx->GetAttribute(key);
  auto attr = std::dynamic_pointer_cast<HttpClientSessionAttribute>(base);
  return attr ? attr->Get() : nullptr;
}

/**
 * @brief Get or create an HttpClientSession in Context.
 */
inline std::shared_ptr<HttpClientSession> GetOrCreateHttpClientSession(
    const std::shared_ptr<Context>& ctx,
    std::string key = std::string(kHttpClientSessionAttributeKey)) {
  if (!ctx) {
    return nullptr;
  }

  if (auto existing = GetHttpClientSession(ctx, key)) {
    return existing;
  }

  auto created = HttpClientSession::Create();
  ctx->SetAttribute(std::move(key),
                    AllocateShared<HttpClientSessionAttribute>(created));
  return created;
}

}  // namespace bsrvcore

#endif  // BSRVCORE_HTTP_CLIENT_SESSION_ATTRIBUTE_H_
