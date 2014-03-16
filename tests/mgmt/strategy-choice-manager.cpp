/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
/**
 * Copyright (C) 2014 Named Data Networking Project
 * See COPYING for copyright and distribution information.
 */

#include "mgmt/strategy-choice-manager.hpp"
#include "face/face.hpp"
#include "mgmt/internal-face.hpp"
#include "table/name-tree.hpp"
#include "table/strategy-choice.hpp"
#include "fw/forwarder.hpp"
#include "fw/strategy.hpp"
#include "tests/face/dummy-face.hpp"
#include "tests/fw/dummy-strategy.hpp"

#include "tests/test-common.hpp"
#include "validation-common.hpp"

namespace nfd {
namespace tests {

NFD_LOG_INIT("StrategyChoiceManagerTest");

class StrategyChoiceManagerFixture : protected BaseFixture
{
public:

  StrategyChoiceManagerFixture()
    : m_strategyChoice(m_forwarder.getStrategyChoice())
    , m_face(make_shared<InternalFace>())
    , m_manager(m_strategyChoice, m_face)
    , m_callbackFired(false)
  {
    m_strategyChoice.install(make_shared<DummyStrategy>(boost::ref(m_forwarder), "/localhost/nfd/strategy/test-strategy-a"));
    m_strategyChoice.insert("ndn:/", "/localhost/nfd/strategy/test-strategy-a");
  }

  virtual
  ~StrategyChoiceManagerFixture()
  {

  }

  void
  validateControlResponseCommon(const Data& response,
                                const Name& expectedName,
                                uint32_t expectedCode,
                                const std::string& expectedText,
                                ControlResponse& control)
  {
    m_callbackFired = true;
    Block controlRaw = response.getContent().blockFromValue();

    control.wireDecode(controlRaw);

    NFD_LOG_DEBUG("received control response"
                  << " Name: " << response.getName()
                  << " code: " << control.getCode()
                  << " text: " << control.getText());

    BOOST_CHECK_EQUAL(response.getName(), expectedName);
    BOOST_CHECK_EQUAL(control.getCode(), expectedCode);
    BOOST_CHECK_EQUAL(control.getText(), expectedText);
  }

  void
  validateControlResponse(const Data& response,
                          const Name& expectedName,
                          uint32_t expectedCode,
                          const std::string& expectedText)
  {
    ControlResponse control;
    validateControlResponseCommon(response, expectedName,
                                  expectedCode, expectedText, control);

    if (!control.getBody().empty())
      {
        BOOST_FAIL("found unexpected control response body");
      }
  }

  void
  validateControlResponse(const Data& response,
                          const Name& expectedName,
                          uint32_t expectedCode,
                          const std::string& expectedText,
                          const Block& expectedBody)
  {
    ControlResponse control;
    validateControlResponseCommon(response, expectedName,
                                  expectedCode, expectedText, control);

    BOOST_REQUIRE(!control.getBody().empty());
    BOOST_REQUIRE(control.getBody().value_size() == expectedBody.value_size());

    BOOST_CHECK(memcmp(control.getBody().value(), expectedBody.value(),
                       expectedBody.value_size()) == 0);

  }

  bool
  didCallbackFire()
  {
    return m_callbackFired;
  }

  void
  resetCallbackFired()
  {
    m_callbackFired = false;
  }

  shared_ptr<InternalFace>&
  getFace()
  {
    return m_face;
  }

  StrategyChoiceManager&
  getManager()
  {
    return m_manager;
  }

  StrategyChoice&
  getStrategyChoice()
  {
    return m_strategyChoice;
  }

  void
  addInterestRule(const std::string& regex,
                  ndn::IdentityCertificate& certificate)
  {
    m_manager.addInterestRule(regex, certificate);
  }

protected:
  Forwarder m_forwarder;
  StrategyChoice& m_strategyChoice;
  shared_ptr<InternalFace> m_face;
  StrategyChoiceManager m_manager;

private:
  bool m_callbackFired;
};

class AllStrategiesFixture : public StrategyChoiceManagerFixture
{
public:
  AllStrategiesFixture()
  {
    m_strategyChoice.install(make_shared<DummyStrategy>(boost::ref(m_forwarder), "/localhost/nfd/strategy/test-strategy-b"));
  }

  virtual
  ~AllStrategiesFixture()
  {

  }
};

template <typename T> class AuthorizedCommandFixture : public CommandFixture<T>
{
public:
  AuthorizedCommandFixture()
  {
    const std::string regex = "^<localhost><nfd><strategy-choice>";
    T::addInterestRule(regex, *CommandFixture<T>::m_certificate);
  }

  virtual
  ~AuthorizedCommandFixture()
  {

  }
};

BOOST_FIXTURE_TEST_SUITE(MgmtStrategyChoiceManager,
                         AuthorizedCommandFixture<AllStrategiesFixture>)

BOOST_FIXTURE_TEST_CASE(TestFireInterestFilter, AllStrategiesFixture)
{
  shared_ptr<Interest> command(make_shared<Interest>("/localhost/nfd/strategy-choice"));

  getFace()->onReceiveData +=
    bind(&StrategyChoiceManagerFixture::validateControlResponse, this,  _1,
         command->getName(), 400, "Malformed command");

  getFace()->sendInterest(*command);

  BOOST_REQUIRE(didCallbackFire());
}

BOOST_FIXTURE_TEST_CASE(MalformedCommmand, AllStrategiesFixture)
{
  shared_ptr<Interest> command(make_shared<Interest>("/localhost/nfd/strategy-choice"));

  getFace()->onReceiveData +=
    bind(&StrategyChoiceManagerFixture::validateControlResponse, this, _1,
         command->getName(), 400, "Malformed command");

  getManager().onStrategyChoiceRequest(*command);

  BOOST_REQUIRE(didCallbackFire());
}

BOOST_FIXTURE_TEST_CASE(UnsignedCommand, AllStrategiesFixture)
{
  ndn::nfd::StrategyChoiceOptions options;
  options.setName("/test");
  options.setStrategy("/localhost/nfd/strategy/best-route");

  Block encodedOptions(options.wireEncode());

  Name commandName("/localhost/nfd/strategy-choice");
  commandName.append("set");
  commandName.append(encodedOptions);

  shared_ptr<Interest> command(make_shared<Interest>(commandName));

  getFace()->onReceiveData +=
    bind(&StrategyChoiceManagerFixture::validateControlResponse, this, _1,
         command->getName(), 401, "Signature required");

  getManager().onStrategyChoiceRequest(*command);

  BOOST_REQUIRE(didCallbackFire());
}

BOOST_FIXTURE_TEST_CASE(UnauthorizedCommand,
                        UnauthorizedCommandFixture<StrategyChoiceManagerFixture>)
{
  ndn::nfd::StrategyChoiceOptions options;
  options.setName("/test");
  options.setStrategy("/localhost/nfd/strategy/best-route");

  Block encodedOptions(options.wireEncode());

  Name commandName("/localhost/nfd/strategy-choice");
  commandName.append("set");
  commandName.append(encodedOptions);

  shared_ptr<Interest> command(make_shared<Interest>(commandName));
  generateCommand(*command);

  getFace()->onReceiveData +=
    bind(&StrategyChoiceManagerFixture::validateControlResponse, this, _1,
         command->getName(), 403, "Unauthorized command");

  getManager().onStrategyChoiceRequest(*command);

  BOOST_REQUIRE(didCallbackFire());
}

BOOST_AUTO_TEST_CASE(UnsupportedVerb)
{
  ndn::nfd::StrategyChoiceOptions options;
  options.setStrategy("/localhost/nfd/strategy/test-strategy-b");

  Block encodedOptions(options.wireEncode());

  Name commandName("/localhost/nfd/strategy-choice");
  commandName.append("unsupported");
  commandName.append(encodedOptions);

  shared_ptr<Interest> command(make_shared<Interest>(commandName));
  generateCommand(*command);

  getFace()->onReceiveData +=
    bind(&StrategyChoiceManagerFixture::validateControlResponse, this, _1,
         command->getName(), 501, "Unsupported command");

  getManager().onValidatedStrategyChoiceRequest(command);

  BOOST_REQUIRE(didCallbackFire());
}

BOOST_AUTO_TEST_CASE(BadOptionParse)
{
  Name commandName("/localhost/nfd/strategy-choice");
  commandName.append("set");
  commandName.append("NotReallyOptions");

  shared_ptr<Interest> command(make_shared<Interest>(commandName));
  generateCommand(*command);

  getFace()->onReceiveData +=
    bind(&StrategyChoiceManagerFixture::validateControlResponse, this, _1,
         command->getName(), 400, "Malformed command");

  getManager().onValidatedStrategyChoiceRequest(command);

  BOOST_REQUIRE(didCallbackFire());
}

BOOST_AUTO_TEST_CASE(SetStrategies)
{
  ndn::nfd::StrategyChoiceOptions options;
  options.setName("/test");
  options.setStrategy("/localhost/nfd/strategy/test-strategy-b");

  Block encodedOptions(options.wireEncode());

  Name commandName("/localhost/nfd/strategy-choice");
  commandName.append("set");
  commandName.append(encodedOptions);

  shared_ptr<Interest> command(make_shared<Interest>(commandName));

  getFace()->onReceiveData +=
    bind(&StrategyChoiceManagerFixture::validateControlResponse, this, _1,
         command->getName(), 200, "Success", encodedOptions);

  getManager().onValidatedStrategyChoiceRequest(command);

  BOOST_REQUIRE(didCallbackFire());
  fw::Strategy& strategy = getStrategyChoice().findEffectiveStrategy("/test");
  BOOST_REQUIRE_EQUAL(strategy.getName(), "/localhost/nfd/strategy/test-strategy-b");

  resetCallbackFired();
  getFace()->onReceiveData.clear();
}

BOOST_AUTO_TEST_CASE(SetUnsupportedStrategy)
{
  ndn::nfd::StrategyChoiceOptions options;
  options.setName("/test");
  options.setStrategy("/localhost/nfd/strategy/unit-test-doesnotexist");

  Block encodedOptions(options.wireEncode());

  Name commandName("/localhost/nfd/strategy-choice");
  commandName.append("set");
  commandName.append(encodedOptions);

  shared_ptr<Interest> command(make_shared<Interest>(commandName));
  generateCommand(*command);

  getFace()->onReceiveData +=
    bind(&StrategyChoiceManagerFixture::validateControlResponse, this, _1,
         command->getName(), 504, "Unsupported strategy");

  getManager().onValidatedStrategyChoiceRequest(command);

  BOOST_REQUIRE(didCallbackFire());
  fw::Strategy& strategy = getStrategyChoice().findEffectiveStrategy("/test");
  BOOST_CHECK_EQUAL(strategy.getName(), "/localhost/nfd/strategy/test-strategy-a");
}

class DefaultStrategyOnlyFixture : public StrategyChoiceManagerFixture
{
public:
  DefaultStrategyOnlyFixture()
    : StrategyChoiceManagerFixture()
  {

  }

  virtual
  ~DefaultStrategyOnlyFixture()
  {

  }
};


/// \todo I'm not sure this code branch (code 405) can happen. The manager tests for the strategy first and will return 504.
// BOOST_FIXTURE_TEST_CASE(SetNotInstalled, DefaultStrategyOnlyFixture)
// {
//   BOOST_REQUIRE(!getStrategyChoice().hasStrategy("/localhost/nfd/strategy/test-strategy-b"));
//   ndn::nfd::StrategyChoiceOptions options;
//   options.setName("/test");
//   options.setStrategy("/localhost/nfd/strategy/test-strategy-b");

//   Block encodedOptions(options.wireEncode());

//   Name commandName("/localhost/nfd/strategy-choice");
//   commandName.append("set");
//   commandName.append(encodedOptions);

//   shared_ptr<Interest> command(make_shared<Interest>(commandName));
//   generateCommand(*command);

//   getFace()->onReceiveData +=
//     bind(&StrategyChoiceManagerFixture::validateControlResponse, this, _1,
//          command->getName(), 405, "Strategy not installed");

//   getManager().onValidatedStrategyChoiceRequest(command);

//   BOOST_REQUIRE(didCallbackFire());
//   fw::Strategy& strategy = getStrategyChoice().findEffectiveStrategy("/test");
//   BOOST_CHECK_EQUAL(strategy.getName(), "/localhost/nfd/strategy/test-strategy-a");
// }

BOOST_AUTO_TEST_CASE(Unset)
{
  ndn::nfd::StrategyChoiceOptions options;
  options.setName("/test");

  BOOST_REQUIRE(m_strategyChoice.insert("/test", "/localhost/nfd/strategy/test-strategy-b"));
  BOOST_REQUIRE_EQUAL(m_strategyChoice.findEffectiveStrategy("/test").getName(),
                      "/localhost/nfd/strategy/test-strategy-b");

  Block encodedOptions(options.wireEncode());

  Name commandName("/localhost/nfd/strategy-choice");
  commandName.append("unset");
  commandName.append(encodedOptions);

  shared_ptr<Interest> command(make_shared<Interest>(commandName));
  generateCommand(*command);

  getFace()->onReceiveData +=
    bind(&StrategyChoiceManagerFixture::validateControlResponse, this, _1,
         command->getName(), 200, "Success", encodedOptions);

  getManager().onValidatedStrategyChoiceRequest(command);

  BOOST_REQUIRE(didCallbackFire());

  BOOST_CHECK_EQUAL(m_strategyChoice.findEffectiveStrategy("/test").getName(),
                    "/localhost/nfd/strategy/test-strategy-a");
}

BOOST_AUTO_TEST_CASE(UnsetRoot)
{
  ndn::nfd::StrategyChoiceOptions options;
  options.setName("/");

  Block encodedOptions(options.wireEncode());

  Name commandName("/localhost/nfd/strategy-choice");
  commandName.append("unset");
  commandName.append(encodedOptions);

  shared_ptr<Interest> command(make_shared<Interest>(commandName));
  generateCommand(*command);

  getFace()->onReceiveData +=
    bind(&StrategyChoiceManagerFixture::validateControlResponse, this, _1,
         command->getName(), 403, "Cannot unset root prefix strategy");

  getManager().onValidatedStrategyChoiceRequest(command);

  BOOST_REQUIRE(didCallbackFire());

  BOOST_CHECK_EQUAL(m_strategyChoice.findEffectiveStrategy("/test").getName(),
                    "/localhost/nfd/strategy/test-strategy-a");
}

BOOST_AUTO_TEST_SUITE_END()

} // namespace tests
} // namespace nfd
