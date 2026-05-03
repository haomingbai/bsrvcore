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

#ifndef BSRVCORE_CONNECTION_CLIENT_HTTP_CLIENT_SESSION_ATTRIBUTE_H_
#define BSRVCORE_CONNECTION_CLIENT_HTTP_CLIENT_SESSION_ATTRIBUTE_H_

#include <memory>
#include <string>
#include <string_view>

#include "bsrvcore/connection/client/http_client_session.h"
#include "bsrvcore/core/trait.h"
#include "bsrvcore/session/attribute.h"
#include "bsrvcore/session/context.h"

namespace bsrvcore {

/**
 * @brief Attribute that stores a shared HttpClientSession.
 */
class HttpClientSessionAttribute
    : public CloneableAttribute<HttpClientSessionAttribute>,
      public CopyableMovable<HttpClientSessionAttribute> {
 public:
  /** @brief Construct an empty session attribute. */
  HttpClientSessionAttribute() = default;
  /**
   * @brief Construct an attribute from a session pointer.
   *
   * @param session Session pointer to store.
   */
  explicit HttpClientSessionAttribute(
      std::shared_ptr<HttpClientSession> session)
      : session_(std::move(session)) {}

  /**
   * @brief Return the stored session pointer.
   *
   * @return Stored session pointer, or null.
   */
  [[nodiscard]] std::shared_ptr<HttpClientSession> Get() const noexcept {
    return session_;
  }
  /**
   * @brief Replace the stored session pointer.
   *
   * @param session New session pointer to store.
   */
  void Set(std::shared_ptr<HttpClientSession> session) {
    session_ = std::move(session);
  }

  [[nodiscard]] std::string ToString() const override {
    return session_ ? "HttpClientSessionAttribute(shared)"
                    : "HttpClientSessionAttribute(null)";
  }

  [[nodiscard]] bool Equals(const Attribute& another) const noexcept override {
    auto* other = dynamic_cast<const HttpClientSessionAttribute*>(&another);
    if (other == nullptr) {
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
 *
 * @param ctx Context to read from.
 * @param key Attribute key used to look up the session.
 * @return Stored session pointer, or null when missing.
 */
inline std::shared_ptr<HttpClientSession> GetHttpClientSession(
    const std::shared_ptr<Context>& ctx,
    const std::string& key = std::string(kHttpClientSessionAttributeKey)) {
  if (!ctx) {
    return nullptr;
  }
  auto base = ctx->GetAttribute(key);
  auto attr = std::dynamic_pointer_cast<HttpClientSessionAttribute>(base);
  return attr ? attr->Get() : nullptr;
}

/**
 * @brief Get or create an HttpClientSession in Context.
 *
 * @param ctx Context to read from and update.
 * @param key Attribute key used to store the session.
 * @return Existing or newly created session, or null when `ctx` is null.
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

#endif  // BSRVCORE_CONNECTION_CLIENT_HTTP_CLIENT_SESSION_ATTRIBUTE_H_
