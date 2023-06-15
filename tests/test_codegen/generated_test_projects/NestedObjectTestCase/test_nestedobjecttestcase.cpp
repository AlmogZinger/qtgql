#include <QSignalSpy>
#include <catch2/catch_test_macros.hpp>

#include "debugableclient.hpp"
#include "graphql/__generated__/MainQuery.hpp"
#include "graphql/__generated__/UpdateUserName.hpp"

namespace NestedObjectTestCase {
using namespace qtgql;
auto ENV_NAME = QString("NestedObjectTestCase");
auto SCHEMA_ADDR = get_server_address("34284866");

TEST_CASE("NestedObjectTestCase", "[generated-testcase]") {
  auto env = test_utils::get_or_create_env(
      ENV_NAME, DebugClientSettings{.prod_settings = {.url = SCHEMA_ADDR}});
  auto mq = std::make_shared<mainquery::MainQuery>();
  mq->fetch();
  test_utils::wait_for_completion(mq);
  SECTION("test deserialize") {
    auto name = mq->get_data()->get_person()->get_name();
    REQUIRE((!name.isEmpty() && name != bases::DEFAULTS::STRING));
  }
  SECTION("test updates") {
    auto user = mq->get_data();
    auto change_user_name_op = updateusername::UpdateUserName::shared();
    QString new_name = "שלום";
    change_user_name_op->set_variables(user->get_id(), new_name);
    change_user_name_op->fetch();
    auto inner_person = user->get_person();
    auto catcher =
        test_utils::SignalCatcher({.source_obj = inner_person, .only = "name"});
    REQUIRE(catcher.wait());

    test_utils::wait_for_completion(change_user_name_op);
    auto new_person = change_user_name_op->get_data()->get_person();
    REQUIRE(user->get_person()->get_id() == new_person->get_id());
    REQUIRE(user->get_person()->get_name() == new_name);
  }
}

}; // namespace NestedObjectTestCase
