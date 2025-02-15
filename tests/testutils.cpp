#include "testutils.hpp"

#include <memory>
using namespace qtgql;

QString get_server_address(const QString &suffix, const QString &prefix) {
  auto env_addr = getenv("SCHEMAS_SERVER_ADDR");
  QString addr = "://localhost:9000/";
  if (env_addr) {
    addr = QString::fromUtf8(env_addr);
  }
  return prefix + addr + suffix;
}

std::shared_ptr<DebugAbleWsNetworkLayer> get_valid_ws_client() {
  auto client = std::shared_ptr<DebugAbleWsNetworkLayer>(
      new DebugAbleWsNetworkLayer({.url = get_server_address()}));
  client->wait_for_valid();
  return client;
}

bool DebugAbleWsNetworkLayer::has_handler(
    const std::shared_ptr<bases::HandlerABC> &handler) {
  for (const auto &uuid_handler_pair : m_connected_handlers) {
    if (uuid_handler_pair.second == handler)
      return true;
  }
  return false;
}

void DebugAbleWsNetworkLayer::onTextMessageReceived(
    const QString &raw_message) {
  auto raw_data = QJsonDocument::fromJson(raw_message.toUtf8());
  if (raw_data.isObject()) {
    auto data = raw_data.object();
    if (m_settings.print_debug) {
      qDebug() << data;
    }
    if (data.contains("id")) {
      // Any that contains ID is directed to a single handler.
      auto message = gqltransportws::GqlTrnsWsMsgWithID(data);
      if (message.type == qtgql::gqltransportws::PROTOCOL::NEXT) {
        m_current_message = message.payload;
      } else if (message.type == qtgql::gqltransportws::PROTOCOL::COMPLETE) {
        if (m_settings.print_debug) {
          qDebug() << "Completed";
        }
      }
    } else {
      auto message = gqltransportws::BaseGqlTrnsWsMsg(data);
      auto message_type = message.type;
      if (message_type == gqltransportws::PROTOCOL::PONG) {
        m_pong_received = true;
        if (!m_settings.handle_pong) {
          return;
        }
      } else if (message_type == gqltransportws::PROTOCOL::CONNECTION_ACK &&
                 !m_settings.handle_ack) {
        return;
      }
    }
  }
  GqlTransportWs::onTextMessageReceived(raw_message);
}

namespace test_utils {

void wait_for_completion(
    const std::shared_ptr<qtgql::bases::OperationHandlerABC> &operation) {
  if (!QTest::qWaitFor([&]() -> bool { return operation->completed(); },
                       1500)) {
    auto error_message =
        operation->ENV_NAME().toStdString() + " Failed to complete!";
    throw std::runtime_error(error_message);
  }
}
std::shared_ptr<qtgql::bases::Environment>
get_or_create_env(const QString &env_name, const DebugClientSettings &settings,
                  std::chrono::milliseconds cache_dur) {
  auto env = bases::Environment::get_env(env_name);
  if (!env.has_value()) {
    auto env_ = std::make_shared<bases::Environment>(
        env_name,
        std::unique_ptr<qtgql::gqltransportws::GqlTransportWs>(
            new DebugAbleWsNetworkLayer(settings)),
        std::unique_ptr<qtgql::bases::EnvCache>(
            new qtgql::bases::EnvCache{{cache_dur}}));
    bases::Environment::set_gql_env(env_);
    DebugAbleWsNetworkLayer::from_environment(env_)->wait_for_valid();
    return env_;
  }
  return env.value();
}

SignalCatcher::SignalCatcher(const SignalCatcherParams &params) {
  m_excludes = params.excludes;
  if (params.exclude_id) {
    m_excludes.insert("id");
    m_excludes.insert("__typeName");
  }
  auto mo = params.source_obj->metaObject();
  for (int i = mo->propertyOffset(); i < mo->propertyCount(); ++i) {
    auto property = mo->property(i);
    QString prop_name = property.name();
    if (!m_excludes.contains(prop_name)) {
      assert_m(property.hasNotifySignal(),
               prop_name + " property has no signal");
      if (params.only.has_value() && params.only.value() != prop_name) {
        continue;
      }
      auto spy = new QSignalSpy(params.source_obj, property.notifySignal());
      if (!spy->isValid()) {
        throw std::runtime_error("spy for property " + prop_name.toStdString() +
                                 " is not valid");
      }
      m_spys.emplace_front(spy, prop_name);
    }
  }
  if (params.only.has_value() && m_spys.empty()) {
    if (m_excludes.contains(params.only.value())) {
      throw std::runtime_error(params.only.value().toStdString() +
                               " is excluded and cannot be included");
    }
    throw std::runtime_error("could not find property signal for " +
                             params.only.value().toStdString());
  }
};
/*
 * Wait for signals emission, returns true if all included
 * signals were caught.
 */
bool SignalCatcher::wait(int timeout) {
  for (const auto &spy_pair : m_spys) {
    if (!QTest::qWaitFor([&]() -> bool { return spy_pair.first->count() != 0; },
                         timeout)) {
      qDebug() << "Signal " << spy_pair.second << " wasn't caught.";
      return false;
    }
    qDebug() << spy_pair.second.toStdString()
             << "was fired:" << spy_pair.first->count() << " times.";
  };
  return true;
}

}; // namespace test_utils
