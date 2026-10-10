#ifndef PEARL_MEDIATOR_ROUTER_HEADER
#define PEARL_MEDIATOR_ROUTER_HEADER

#include "MOOS/libMOOS/Thirdparty/AppCasting/AppCastingMOOSApp.h"
#include <map>
#include <set>
#include <string>

// Addressing belongs to the mediated protocol, not pShare's opaque transport.
// This dispatcher preserves end-to-end envelopes/ACKs; it never acknowledges,
// executes, de-duplicates, rate limits, or chooses alternate network routes.
class MediatorRouter : public AppCastingMOOSApp
{
public:
  ~MediatorRouter() override;

protected:
  bool OnStartUp() override;
  bool OnConnectToServer() override;
  bool OnNewMail(MOOSMSG_LIST &mail) override;
  bool Iterate() override;
  bool buildReport() override;

private:
  void registerVariables();
  void dispatch(const CMOOSMsg &message, bool ack, bool incoming);
  void reject(const std::string &reason);
  std::string m_ownship;
  std::set<std::string> m_peers;
  std::map<std::string, unsigned long> m_message_posts, m_ack_posts;
  unsigned long m_local_messages = 0, m_local_acks = 0, m_rejected = 0;
  bool m_allow_relay = false, m_valid = false;
};
#endif
