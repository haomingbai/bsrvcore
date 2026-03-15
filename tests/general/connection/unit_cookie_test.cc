#include <gtest/gtest.h>

#include <string>

#include "bsrvcore/server_set_cookie.h"

// Ensure missing name/value returns an empty cookie string.
TEST(ServerSetCookieTest, MissingNameOrValueReturnsEmpty) {
  bsrvcore::ServerSetCookie cookie;
  cookie.SetName("session");
  EXPECT_TRUE(cookie.ToString().empty());

  bsrvcore::ServerSetCookie cookie2;
  cookie2.SetValue("abc");
  EXPECT_TRUE(cookie2.ToString().empty());
}

// Ensure attributes are rendered into the Set-Cookie string.
TEST(ServerSetCookieTest, BuildsCookieWithAttributes) {
  bsrvcore::ServerSetCookie cookie;
  cookie.SetName("sid")
      .SetValue("abc")
      .SetPath("/")
      .SetDomain("example.com")
      .SetMaxAge(3600)
      .SetSameSite(bsrvcore::SameSite::kNone)
      .SetHttpOnly(true);

  auto result = cookie.ToString();
  ASSERT_FALSE(result.empty());
  EXPECT_NE(result.find("sid=abc"), std::string::npos);
  EXPECT_NE(result.find("Path=/"), std::string::npos);
  EXPECT_NE(result.find("Domain=example.com"), std::string::npos);
  EXPECT_NE(result.find("Max-Age=3600"), std::string::npos);
  EXPECT_NE(result.find("SameSite=None"), std::string::npos);
  EXPECT_NE(result.find("Secure"), std::string::npos);
  EXPECT_NE(result.find("HttpOnly"), std::string::npos);

  EXPECT_FALSE(result.back() == ';' || result.back() == ' ');
}

// Ensure SameSite=Strict does not force Secure flag.
TEST(ServerSetCookieTest, SameSiteStrictDoesNotForceSecure) {
  bsrvcore::ServerSetCookie cookie;
  cookie.SetName("sid").SetValue("abc").SetSameSite(
      bsrvcore::SameSite::kStrict);

  auto result = cookie.ToString();
  ASSERT_FALSE(result.empty());
  EXPECT_NE(result.find("SameSite=Strict"), std::string::npos);
  EXPECT_EQ(result.find("Secure"), std::string::npos);
}
