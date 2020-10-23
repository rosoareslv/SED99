/**
 *    Copyright (C) 2018 MongoDB Inc.
 *
 *    This program is free software: you can redistribute it and/or modify
 *    it under the terms of the GNU Affero General Public License, version 3,
 *    as published by the Free Software Foundation.
 *
 *    This program is distributed in the hope that it will be useful,
 *    but WITHOUT ANY WARRANTY; without even the implied warranty of
 *    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *    GNU Affero General Public License for more details.
 *
 *    You should have received a copy of the GNU Affero General Public License
 *    along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 *    As a special exception, the copyright holders give permission to link the
 *    code of portions of this program with the OpenSSL library under certain
 *    conditions as described in each individual source file and distribute
 *    linked combinations including the program with the OpenSSL library. You
 *    must comply with the GNU Affero General Public License in all respects for
 *    all of the code used other than as permitted herein. If you modify file(s)
 *    with this exception, you may extend this exception to your version of the
 *    file(s), but you are not obligated to do so. If you do not wish to do so,
 *    delete this exception statement from your version. If you delete this
 *    exception statement from all source files in the program, then also delete
 *    it in the license file.
 */

#include "mongo/platform/basic.h"

#include "mongo/config.h"

#include "mongo/util/net/ssl_options.h"

#include <ostream>

#include "mongo/db/server_options_server_helpers.h"
#include "mongo/unittest/unittest.h"
#include "mongo/util/net/ssl_options.h"
#include "mongo/util/options_parser/environment.h"
#include "mongo/util/options_parser/option_section.h"
#include "mongo/util/options_parser/options_parser.h"

namespace moe = mongo::optionenvironment;

namespace mongo {
Status addSSLServerOptions(moe::OptionSection* options);
Status storeSSLServerOptions(const moe::Environment& params);

namespace {

namespace test {
struct Vector : public std::vector<uint8_t> {
    Vector(std::vector<uint8_t> v) : std::vector<uint8_t>(std::move(v)) {}
};
std::ostream& operator<<(std::ostream& ss, const Vector& val) {
    ss << '{';
    std::string comma;
    for (const auto& b : val) {
        ss << comma << b;
        comma = ", ";
    }
    ss << '}';
    return ss;
}
}  // namespace test

TEST(SSLOptions, validCases) {
    SSLParams::CertificateSelector selector;

    ASSERT_OK(parseCertificateSelector(&selector, "subj", "subject=test.example.com"));
    ASSERT_EQ(selector.subject, "test.example.com");

    ASSERT_OK(parseCertificateSelector(&selector, "hash", "thumbprint=0123456789"));
    ASSERT_EQ(test::Vector(selector.thumbprint), test::Vector({0x01, 0x23, 0x45, 0x67, 0x89}));
}

TEST(SSLOptions, invalidCases) {
    SSLParams::CertificateSelector selector;

    auto status = parseCertificateSelector(&selector, "option", "bogus=nothing");
    ASSERT_NOT_OK(status);
    ASSERT_EQ(status.reason(), "Unknown certificate selector property for 'option': 'bogus'");

    status = parseCertificateSelector(&selector, "option", "thumbprint=0123456");
    ASSERT_NOT_OK(status);
    ASSERT_EQ(status.reason(),
              "Invalid certificate selector value for 'option': Not an even number of hexits");

    status = parseCertificateSelector(&selector, "option", "thumbprint=bogus");
    ASSERT_NOT_OK(status);
    ASSERT_EQ(status.reason(),
              "Invalid certificate selector value for 'option': Not a valid hex string");
}

class OptionsParserTester : public moe::OptionsParser {
public:
    Status readConfigFile(const std::string& filename, std::string* config) {
        if (filename != _filename) {
            ::mongo::StringBuilder sb;
            sb << "Parser using filename: " << filename
               << " which does not match expected filename: " << _filename;
            return Status(ErrorCodes::InternalError, sb.str());
        }
        *config = _config;
        return Status::OK();
    }
    void setConfig(const std::string& filename, const std::string& config) {
        _filename = filename;
        _config = config;
    }

private:
    std::string _filename;
    std::string _config;
};

TEST(SetupOptions, tlsModeDisabled) {
    OptionsParserTester parser;
    moe::Environment environment;
    moe::OptionSection options;

    ASSERT_OK(::mongo::addGeneralServerOptions(&options));

    std::vector<std::string> argv;
    argv.push_back("binaryname");
    argv.push_back("--tlsMode");
    argv.push_back("disabled");
    std::map<std::string, std::string> env_map;

    ASSERT_OK(::mongo::addSSLServerOptions(&options));

    ASSERT_OK(parser.run(options, argv, env_map, &environment));
    ASSERT_OK(::mongo::storeSSLServerOptions(environment));
    ASSERT_EQ(::mongo::sslGlobalParams.sslMode.load(), ::mongo::sslGlobalParams.SSLMode_disabled);
}

TEST(SetupOptions, sslModeDisabled) {
    OptionsParserTester parser;
    moe::Environment environment;
    moe::OptionSection options;

    ASSERT_OK(::mongo::addGeneralServerOptions(&options));

    std::vector<std::string> argv;
    argv.push_back("binaryname");
    argv.push_back("--sslMode");
    argv.push_back("disabled");
    std::map<std::string, std::string> env_map;

    ASSERT_OK(::mongo::addSSLServerOptions(&options));

    ASSERT_OK(parser.run(options, argv, env_map, &environment));
    ASSERT_OK(::mongo::storeSSLServerOptions(environment));
    ASSERT_EQ(::mongo::sslGlobalParams.sslMode.load(), ::mongo::sslGlobalParams.SSLMode_disabled);
}

TEST(SetupOptions, tlsModeRequired) {
    OptionsParserTester parser;
    moe::Environment environment;
    moe::OptionSection options;

    ASSERT_OK(::mongo::addGeneralServerOptions(&options));

    std::string sslPEMKeyFile = "jstests/libs/server.pem";
    std::string sslCAFFile = "jstests/libs/ca.pem";
    std::string sslCRLFile = "jstests/libs/crl.pem";
    std::string sslClusterFile = "jstests/libs/cluster_cert.pem";

    std::vector<std::string> argv;
    argv.push_back("binaryname");
    argv.push_back("--tlsMode");
    argv.push_back("requireTLS");
    argv.push_back("--tlsPEMKeyFile");
    argv.push_back(sslPEMKeyFile);
    argv.push_back("--tlsCAFile");
    argv.push_back(sslCAFFile);
    argv.push_back("--tlsCRLFile");
    argv.push_back(sslCRLFile);
    argv.push_back("--tlsClusterFile");
    argv.push_back(sslClusterFile);
    argv.push_back("--tlsAllowInvalidHostnames");
    argv.push_back("--tlsAllowInvalidCertificates");
    argv.push_back("--tlsWeakCertificateValidation");
    argv.push_back("--tlsFIPSMode");
    argv.push_back("--tlsPEMKeyPassword");
    argv.push_back("pw1");
    argv.push_back("--tlsClusterPassword");
    argv.push_back("pw2");
    argv.push_back("--tlsDisabledProtocols");
    argv.push_back("TLS1_1");
    std::map<std::string, std::string> env_map;

    ASSERT_OK(mongo::addSSLServerOptions(&options));

    ASSERT_OK(parser.run(options, argv, env_map, &environment));
    ASSERT_OK(mongo::storeSSLServerOptions(environment));

    ASSERT_EQ(::mongo::sslGlobalParams.sslMode.load(), ::mongo::sslGlobalParams.SSLMode_requireSSL);
    ASSERT_EQ(::mongo::sslGlobalParams.sslPEMKeyFile.substr(
                  ::mongo::sslGlobalParams.sslPEMKeyFile.length() - sslPEMKeyFile.length()),
              sslPEMKeyFile);
    ASSERT_EQ(::mongo::sslGlobalParams.sslCAFile.substr(
                  ::mongo::sslGlobalParams.sslCAFile.length() - sslCAFFile.length()),
              sslCAFFile);
    ASSERT_EQ(::mongo::sslGlobalParams.sslCRLFile.substr(
                  ::mongo::sslGlobalParams.sslCRLFile.length() - sslCRLFile.length()),
              sslCRLFile);
    ASSERT_EQ(::mongo::sslGlobalParams.sslClusterFile.substr(
                  ::mongo::sslGlobalParams.sslClusterFile.length() - sslClusterFile.length()),
              sslClusterFile);
    ASSERT_EQ(::mongo::sslGlobalParams.sslAllowInvalidHostnames, true);
    ASSERT_EQ(::mongo::sslGlobalParams.sslAllowInvalidCertificates, true);
    ASSERT_EQ(::mongo::sslGlobalParams.sslWeakCertificateValidation, true);
    ASSERT_EQ(::mongo::sslGlobalParams.sslFIPSMode, true);
    ASSERT_EQ(::mongo::sslGlobalParams.sslPEMKeyPassword, "pw1");
    ASSERT_EQ(::mongo::sslGlobalParams.sslClusterPassword, "pw2");
    ASSERT_EQ(static_cast<int>(::mongo::sslGlobalParams.sslDisabledProtocols.back()),
              static_cast<int>(::mongo::SSLParams::Protocols::TLS1_1));
}

TEST(SetupOptions, sslModeRequired) {
    OptionsParserTester parser;
    moe::Environment environment;
    moe::OptionSection options;

    ASSERT_OK(::mongo::addGeneralServerOptions(&options));

    std::string sslPEMKeyFile = "jstests/libs/server.pem";
    std::string sslCAFFile = "jstests/libs/ca.pem";
    std::string sslCRLFile = "jstests/libs/crl.pem";
    std::string sslClusterFile = "jstests/libs/cluster_cert.pem";

    std::vector<std::string> argv;
    argv.push_back("binaryname");
    argv.push_back("--sslMode");
    argv.push_back("requireSSL");
    argv.push_back("--sslPEMKeyFile");
    argv.push_back(sslPEMKeyFile);
    argv.push_back("--sslCAFile");
    argv.push_back(sslCAFFile);
    argv.push_back("--sslCRLFile");
    argv.push_back(sslCRLFile);
    argv.push_back("--sslClusterFile");
    argv.push_back(sslClusterFile);
    argv.push_back("--sslAllowInvalidHostnames");
    argv.push_back("--sslAllowInvalidCertificates");
    argv.push_back("--sslWeakCertificateValidation");
    argv.push_back("--sslFIPSMode");
    argv.push_back("--sslPEMKeyPassword");
    argv.push_back("pw1");
    argv.push_back("--sslClusterPassword");
    argv.push_back("pw2");
    argv.push_back("--sslDisabledProtocols");
    argv.push_back("TLS1_1");
    std::map<std::string, std::string> env_map;

    ASSERT_OK(mongo::addSSLServerOptions(&options));

    ASSERT_OK(parser.run(options, argv, env_map, &environment));
    ASSERT_OK(mongo::storeSSLServerOptions(environment));

    ASSERT_EQ(::mongo::sslGlobalParams.sslMode.load(), ::mongo::sslGlobalParams.SSLMode_requireSSL);
    ASSERT_EQ(::mongo::sslGlobalParams.sslPEMKeyFile.substr(
                  ::mongo::sslGlobalParams.sslPEMKeyFile.length() - sslPEMKeyFile.length()),
              sslPEMKeyFile);
    ASSERT_EQ(::mongo::sslGlobalParams.sslCAFile.substr(
                  ::mongo::sslGlobalParams.sslCAFile.length() - sslCAFFile.length()),
              sslCAFFile);
    ASSERT_EQ(::mongo::sslGlobalParams.sslCRLFile.substr(
                  ::mongo::sslGlobalParams.sslCRLFile.length() - sslCRLFile.length()),
              sslCRLFile);
    ASSERT_EQ(::mongo::sslGlobalParams.sslClusterFile.substr(
                  ::mongo::sslGlobalParams.sslClusterFile.length() - sslClusterFile.length()),
              sslClusterFile);
    ASSERT_EQ(::mongo::sslGlobalParams.sslAllowInvalidHostnames, true);
    ASSERT_EQ(::mongo::sslGlobalParams.sslAllowInvalidCertificates, true);
    ASSERT_EQ(::mongo::sslGlobalParams.sslWeakCertificateValidation, true);
    ASSERT_EQ(::mongo::sslGlobalParams.sslFIPSMode, true);
    ASSERT_EQ(::mongo::sslGlobalParams.sslPEMKeyPassword, "pw1");
    ASSERT_EQ(::mongo::sslGlobalParams.sslClusterPassword, "pw2");
    ASSERT_EQ(static_cast<int>(::mongo::sslGlobalParams.sslDisabledProtocols.back()),
              static_cast<int>(::mongo::SSLParams::Protocols::TLS1_1));
}

#ifdef MONGO_CONFIG_SSL_CERTIFICATE_SELECTORS
TEST(SetupOptions, tlsModeRequiredCertificateSelector) {
    OptionsParserTester parser;
    moe::Environment environment;
    moe::OptionSection options;

    ASSERT_OK(::mongo::addGeneralServerOptions(&options));

    std::vector<std::string> argv;
    argv.push_back("binaryname");
    argv.push_back("--tlsMode");
    argv.push_back("requireTLS");
    argv.push_back("--tlsCertificateSelector");
    argv.push_back("subject=Subject 1");
    argv.push_back("--tlsClusterCertificateSelector");
    argv.push_back("subject=Subject 2");
    std::map<std::string, std::string> env_map;

    ASSERT_OK(mongo::addSSLServerOptions(&options));

    ASSERT_OK(parser.run(options, argv, env_map, &environment));
    ASSERT_OK(mongo::storeSSLServerOptions(environment));

    ASSERT_EQ(::mongo::sslGlobalParams.sslMode.load(), ::mongo::sslGlobalParams.SSLMode_requireSSL);
    ASSERT_EQ(::mongo::sslGlobalParams.sslCertificateSelector.subject, "Subject 1");
    ASSERT_EQ(::mongo::sslGlobalParams.sslClusterCertificateSelector.subject, "Subject 2");
}

TEST(SetupOptions, sslModeRequiredCertificateSelector) {
    OptionsParserTester parser;
    moe::Environment environment;
    moe::OptionSection options;

    ASSERT_OK(::mongo::addGeneralServerOptions(&options));

    std::vector<std::string> argv;
    argv.push_back("binaryname");
    argv.push_back("--sslMode");
    argv.push_back("requireSSL");
    argv.push_back("--sslCertificateSelector");
    argv.push_back("subject=Subject 1");
    argv.push_back("--sslClusterCertificateSelector");
    argv.push_back("subject=Subject 2");
    std::map<std::string, std::string> env_map;

    ASSERT_OK(mongo::addSSLServerOptions(&options));

    ASSERT_OK(parser.run(options, argv, env_map, &environment));
    ASSERT_OK(mongo::storeSSLServerOptions(environment));

    ASSERT_EQ(::mongo::sslGlobalParams.sslMode.load(), ::mongo::sslGlobalParams.SSLMode_requireSSL);
    ASSERT_EQ(::mongo::sslGlobalParams.sslCertificateSelector.subject, "Subject 1");
    ASSERT_EQ(::mongo::sslGlobalParams.sslClusterCertificateSelector.subject, "Subject 2");
}

TEST(SetupOptions, disableNonSSLConnectionLoggingFalse) {
    OptionsParserTester parser;
    moe::Environment environment;
    moe::OptionSection options;

    ASSERT_OK(::mongo::addGeneralServerOptions(&options));

    std::vector<std::string> argv;
    argv.push_back("binaryname");
    argv.push_back("--setParameter");
    argv.push_back("disableNonSSLConnectionLogging=false");
    std::map<std::string, std::string> env_map;

    ASSERT_OK(parser.run(options, argv, env_map, &environment));
    Status storeRet = mongo::storeServerOptions(environment);

    ASSERT_EQ(::mongo::sslGlobalParams.disableNonSSLConnectionLogging, false);
}

TEST(SetupOptions, disableNonTLSConnectionLoggingFalse) {
    OptionsParserTester parser;
    moe::Environment environment;
    moe::OptionSection options;

    ::mongo::sslGlobalParams.disableNonSSLConnectionLoggingSet = false;
    ASSERT_OK(::mongo::addGeneralServerOptions(&options));

    std::vector<std::string> argv;
    argv.push_back("binaryname");
    argv.push_back("--setParameter");
    argv.push_back("disableNonTLSConnectionLogging=false");
    std::map<std::string, std::string> env_map;

    ASSERT_OK(parser.run(options, argv, env_map, &environment));
    Status storeRet = mongo::storeServerOptions(environment);

    ASSERT_EQ(::mongo::sslGlobalParams.disableNonSSLConnectionLogging, false);
}

TEST(SetupOptions, disableNonSSLConnectionLoggingTrue) {
    OptionsParserTester parser;
    moe::Environment environment;
    moe::OptionSection options;

    ::mongo::sslGlobalParams.disableNonSSLConnectionLoggingSet = false;
    ASSERT_OK(::mongo::addGeneralServerOptions(&options));

    std::vector<std::string> argv;
    argv.push_back("binaryname");
    argv.push_back("--setParameter");
    argv.push_back("disableNonSSLConnectionLogging=true");
    std::map<std::string, std::string> env_map;

    ASSERT_OK(parser.run(options, argv, env_map, &environment));
    Status storeRet = mongo::storeServerOptions(environment);

    ASSERT_EQ(::mongo::sslGlobalParams.disableNonSSLConnectionLogging, true);
}

TEST(SetupOptions, disableNonTLSConnectionLoggingTrue) {
    OptionsParserTester parser;
    moe::Environment environment;
    moe::OptionSection options;

    ::mongo::sslGlobalParams.disableNonSSLConnectionLoggingSet = false;
    ASSERT_OK(::mongo::addGeneralServerOptions(&options));

    std::vector<std::string> argv;
    argv.push_back("binaryname");
    argv.push_back("--setParameter");
    argv.push_back("disableNonTLSConnectionLogging=true");
    std::map<std::string, std::string> env_map;

    ASSERT_OK(parser.run(options, argv, env_map, &environment));
    Status storeRet = mongo::storeServerOptions(environment);

    ASSERT_EQ(::mongo::sslGlobalParams.disableNonSSLConnectionLogging, true);
}

TEST(SetupOptions, disableNonTLSConnectionLoggingInvalid) {
    OptionsParserTester parser;
    moe::Environment environment;
    moe::OptionSection options;

    ASSERT_OK(::mongo::addGeneralServerOptions(&options));

    std::vector<std::string> argv;
    argv.push_back("binaryname");
    argv.push_back("--setParameter");
    argv.push_back("disableNonTLSConnectionLogging=false");
    argv.push_back("--setParameter");
    argv.push_back("disableNonSSLConnectionLogging=false");
    std::map<std::string, std::string> env_map;

    ASSERT_OK(parser.run(options, argv, env_map, &environment));
    ASSERT_NOT_OK(mongo::storeServerOptions(environment));
}
#endif

}  // namespace
}  // namespace mongo
