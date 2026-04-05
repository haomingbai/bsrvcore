#pragma once

#include <boost/asio/buffer.hpp>
#include <boost/asio/ssl/context.hpp>
#include <boost/system/error_code.hpp>
#include <stdexcept>

namespace bsrvcore::test {

inline void ConfigureTestServerSslContext(boost::asio::ssl::context& ctx) {
  static constexpr char kCertificatePem[] =
      "-----BEGIN CERTIFICATE-----\n"
      "MIIDCTCCAfGgAwIBAgIURB1b4zDUugHCbAK1FLOVMZg9qpgwDQYJKoZIhvcNAQEL\n"
      "BQAwFDESMBAGA1UEAwwJMTI3LjAuMC4xMB4XDTI2MDQwNTExMDUzNVoXDTM2MDQw\n"
      "MjExMDUzNVowFDESMBAGA1UEAwwJMTI3LjAuMC4xMIIBIjANBgkqhkiG9w0BAQEF\n"
      "AAOCAQ8AMIIBCgKCAQEA6q5Co+p8QaF4Mi99pY4OWdffNT6vEOakeM3BNA+HoQap\n"
      "qzJcW5niSjMXNizqS7Ms60493Txy3eVKxxDQ8ZzSwSNSU3JEFqCSiQrTb/YD5/vt\n"
      "8r3GdrSNrgaBTPPIXYhYyH1nXTZmjagix0isi2lRyU36MM54oCUaa2jzFqowoprO\n"
      "TheBk6Nu8ID8s/L5+BNOVWCQKhx0V5ejbJ9ureld4yxwzRKLwF+nStAWG9JeNxQo\n"
      "LPPpxQE66tqtkdPsjple3LULFCE6mVWvljaV6HNAf45bkd7IsE6KHKmyADsUsDmT\n"
      "mtbJWnutYs4xAdCLmJzyGFSY1URdHATXj9GOL6MHNwIDAQABo1MwUTAdBgNVHQ4E\n"
      "FgQUwF3UBUPQaGGzuA9Q5BlDzm/HlkMwHwYDVR0jBBgwFoAUwF3UBUPQaGGzuA9Q\n"
      "5BlDzm/HlkMwDwYDVR0TAQH/BAUwAwEB/zANBgkqhkiG9w0BAQsFAAOCAQEA1BOy\n"
      "j4NmdJ6SDMP6y4oe1H+d4ZweJDkp0NnpRAhXZTV3tB4IOhM8/tnEoKw/yfZXk4ar\n"
      "UAJEs1Vs9LuiX7/qhrsOVMOqnw1wJuhkJHcg5IDNsJGcMzGJcc4Tyx0wmokSvobO\n"
      "wGG7efsHQbfc8lcJExab1i6aJk34gsuEsIibemdcjd5fjq4SmJhCzdR8gZvBUQVP\n"
      "JOOk1nh9szieFsrCiqVxiu/rk2QOW40nkmxkqQTTVivGrQzdoGBSktPceprO8EIk\n"
      "ofY4FAQzspcKEG96dizBcb0N+INQlYIDCzbUf+2hh1V9sTp7qiFReXWIlS6Z+JLa\n"
      "RzIlgNyRISRd251Z0A==\n"
      "-----END CERTIFICATE-----\n";

  static constexpr char kPrivateKeyPem[] =
      "-----BEGIN PRIVATE KEY-----\n"
      "MIIEvQIBADANBgkqhkiG9w0BAQEFAASCBKcwggSjAgEAAoIBAQDqrkKj6nxBoXgy\n"
      "L32ljg5Z1981Pq8Q5qR4zcE0D4ehBqmrMlxbmeJKMxc2LOpLsyzrTj3dPHLd5UrH\n"
      "ENDxnNLBI1JTckQWoJKJCtNv9gPn++3yvcZ2tI2uBoFM88hdiFjIfWddNmaNqCLH\n"
      "SKyLaVHJTfowznigJRpraPMWqjCims5OF4GTo27wgPyz8vn4E05VYJAqHHRXl6Ns\n"
      "n26t6V3jLHDNEovAX6dK0BYb0l43FCgs8+nFATrq2q2R0+yOmV7ctQsUITqZVa+W\n"
      "NpXoc0B/jluR3siwToocqbIAOxSwOZOa1slae61izjEB0IuYnPIYVJjVRF0cBNeP\n"
      "0Y4vowc3AgMBAAECggEAHhW32k6ZnkpGX9yhtWMIKPFZHnjKNZbzt77cyUFCaFyz\n"
      "zIwYte98yHlTPPE2Gf0+wW2W1bFFoneeBfX8azTo+bR+/c6UtvffwSy/nUdDCe7M\n"
      "esVkV1xfG1OfKM/JvF/Zzd3xrcvnCrrmtnh1EiwuvWk/LRmS8ii/gcyC+UFZGccS\n"
      "2IVJdsZKwALwzSJJd7UVH3dccU+icGe+9AkyZMg9lpCcwg3HaOHWVMpKL8exB5PN\n"
      "3ZCjvLVPdhoyLFEcbXxpkTq6Ip73paPHJUaYEm/hzD5DHkvX0KyYynhMF680TvLz\n"
      "VPJcUodyauh8DOVyRR6lF6APSGZinYlSg80noq8wdQKBgQD439uNUBWYK4mgYAfB\n"
      "oFeYtEOwydIu9Uk0Evuht6s/IgHcBZbxWuyWsAsGJLwEiq27ga8iaoA0FsM3kjbw\n"
      "njwBSD0d3IsvLeYLEiJWXcK7PlG6yD3jsY9p6h9jTqO082OF+P/6usoHt1fegGuB\n"
      "dqy+EzqAKz3zM5jPyRP6RYOCfQKBgQDxZl5i9lVYba1OpOhKs5X2HoQA60+BHuZC\n"
      "gPz9m+xbF7J4iwMy9Vr0vDUuIQya/pmaEw1IQ8GS49MytBUy826QjZ5qUcXIuy4q\n"
      "4+rjiM0/Ti9gpRhh1U8dycd11X32KsRHvjd7jHVsX7rpx/cM416EHopWkRRiUrSz\n"
      "mNyVbYDKwwKBgCGmOswvgMvzTwdlFIdkg5N9BA36K7X3qi8lReqGp9vAYFn8U31M\n"
      "muKA6OyquNUwXu8USLaxiaYBUeHIni8IZfqSZtkLwbHeVdU3XXcp9DNW6LwFaQDJ\n"
      "OCfM5POLZW0I33L0yL+A0+IZMwM9f8ugXRjSBr3fmt+wIUAu4sma7n4NAoGAH/Ba\n"
      "Zp+O9S81seURsuiF29V3w6NeloffEUd9sZRStk6xV0+VMcXSrfTE2ICY6VzsN71z\n"
      "kW8dinDIPboj3+TjaQ7Due9tyrwxRI15Q3eTKGAQ1qmhSzhsylUrJcUEcHCCHbfm\n"
      "IVuZIaic01eYsUTX+YUM6p0xZDzrGaQM81xUoP8CgYEAosTRgYchiXfCKRyMBi+G\n"
      "OLaWGR5ByVdp7oaY5mSkutdKfScHiIBfmuRJl9JhXsSGS4rPFSfpm4CxN8fD+gHD\n"
      "Lau+wziWKcxWw2/YlqiJLEmpobM6WLXbOnaHEu6hjnq88qU3lafFcmYXknEPUmiI\n"
      "zlUjRwO56lPlEZAcfRrnIrM=\n"
      "-----END PRIVATE KEY-----\n";

  ctx.set_options(boost::asio::ssl::context::default_workarounds |
                  boost::asio::ssl::context::no_sslv2 |
                  boost::asio::ssl::context::single_dh_use);

  boost::system::error_code ec;
  ctx.use_certificate_chain(
      boost::asio::buffer(kCertificatePem, sizeof(kCertificatePem) - 1), ec);
  if (ec) {
    throw std::runtime_error("failed to load test certificate");
  }

  ctx.use_private_key(
      boost::asio::buffer(kPrivateKeyPem, sizeof(kPrivateKeyPem) - 1),
      boost::asio::ssl::context::file_format::pem, ec);
  if (ec) {
    throw std::runtime_error("failed to load test private key");
  }
}

}  // namespace bsrvcore::test
