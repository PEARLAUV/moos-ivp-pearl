#include "MediatorRouter.h"
#include "AckMessage.h"
#include "MBUtils.h"
#include "NodeMessageUtils.h"
#include <cctype>
#include <iomanip>

namespace
{
bool validName(const std::string &name)
{
  if (name.empty() || !std::isalpha(static_cast<unsigned char>(name.front())))
    return false;
  for (const unsigned char ch : name)
    if (!std::isalnum(ch) && ch != '_')
      return false;
  return true;
}

// Reject ambiguous addressing even though the upstream utility parsers accept
// repeated keys. Do not interpret or reserialize the application payload.
bool uniqueAddresses(const std::string &data, bool ack)
{
  std::set<std::string> seen;
  for (std::string field : parseStringQ(data, ',')) {
    const std::string key = tolower(biteStringX(field, '='));
    if (key == (ack ? "src" : "src_node") || key == (ack ? "dest" : "dest_node") ||
        key == (ack ? "id" : "ack_id")) {
      if (!seen.insert(key).second)
        return false;
    }
  }
  return seen.size() == 3;
}
} // namespace

MediatorRouter::~MediatorRouter()
{
  m_Comms.Close();
}

bool MediatorRouter::OnStartUp()
{
  AppCastingMOOSApp::OnStartUp();
  STRING_LIST lines;
  m_MissionReader.GetConfiguration(GetAppName(), lines);
  bool ok = true;
  for (std::string line : lines) {
    const std::string original = line;
    const std::string key = tolower(biteStringX(line, '='));
    const std::string value = stripBlankEnds(line);
    bool handled = false;
    if (key == "vname") {
      handled = validName(value);
      if (handled)
        m_ownship = value;
    } else if (key == "peer") {
      handled = validName(value);
      if (handled)
        m_peers.insert(value);
    } else if (key == "allow_relay")
      handled = setBooleanOnString(m_allow_relay, value);
    else if (key == "apptick" || key == "commstick")
      handled = true;
    if (!handled) {
      reportUnhandledConfigWarning(original);
      ok = false;
    }
  }
  std::set<std::string> suffixes;
  suffixes.insert(toupper(m_ownship));
  for (const auto &peer : m_peers)
    if (!suffixes.insert(toupper(peer)).second)
      ok = false;
  m_valid = ok && validName(m_ownship) && !m_peers.empty();
  if (!m_valid)
    reportConfigWarning("Require vname and distinct peer names with unique uppercase suffixes");
  registerVariables();
  return true;
}

bool MediatorRouter::OnConnectToServer()
{
  registerVariables();
  return true;
}

void MediatorRouter::registerVariables()
{
  AppCastingMOOSApp::RegisterVariables();
  for (const auto &key :
       {"MEDIATED_MESSAGE_LOCAL", "ACK_MESSAGE_LOCAL", "MEDIATED_MESSAGE_IN", "ACK_MESSAGE_IN"})
    Register(key, 0);
}

void MediatorRouter::reject(const std::string &reason)
{
  ++m_rejected;
  reportRunWarning("Mediated routing rejected: " + reason);
}

void MediatorRouter::dispatch(const CMOOSMsg &message, bool ack, bool incoming)
{
  if (!m_valid) {
    reject("invalid configuration");
    return;
  }
  if (!message.IsString() || !uniqueAddresses(message.GetString(), ack)) {
    reject("missing/duplicate addressing or non-string envelope");
    return;
  }
  std::string src, dest;
  bool valid = false;
  if (ack) {
    const AckMessage parsed = string2AckMessage(message.GetString());
    valid = parsed.valid();
    src = parsed.getSourceNode();
    dest = parsed.getDestNode();
  } else {
    const NodeMessage parsed = string2NodeMessage(message.GetString());
    valid = parsed.valid() && !parsed.getMessageID().empty() && parsed.getDestGroup().empty();
    src = parsed.getSourceNode();
    dest = parsed.getDestNode();
  }
  const auto known = [this](const std::string &name) {
    return name == m_ownship || m_peers.count(name) != 0;
  };
  if (!valid || !known(src) || !known(dest) || src == dest) {
    reject("invalid envelope, unknown endpoint, or unexpanded group");
    return;
  }
  if ((!incoming && src != m_ownship) || (incoming && src == m_ownship)) {
    reject("source inconsistent with local/incoming channel");
    return;
  }
  const bool local = incoming && dest == m_ownship;
  if (incoming && !local && !m_allow_relay) {
    reject("transit disabled");
    return;
  }
  const std::string key = local ? (ack ? "ACK_MESSAGE" : "MEDIATED_MESSAGE")
                                : (ack ? "ACK_TX_" : "MEDIATED_TX_") + toupper(dest);
  CMOOSMsg copy = message;
  copy.m_sKey = key;
  // Copy rather than Notify: retain the original timestamp and exact serialized
  // string value, including quoted commas, message ID, and ack policy.
  if (!m_Comms.Post(copy)) {
    reject("local MOOS post failed");
    return;
  }
  if (local) {
    if (ack)
      ++m_local_acks;
    else
      ++m_local_messages;
  } else {
    if (ack)
      ++m_ack_posts[dest];
    else
      ++m_message_posts[dest];
  }
}

bool MediatorRouter::OnNewMail(MOOSMSG_LIST &mail)
{
  AppCastingMOOSApp::OnNewMail(mail);
  for (const auto &message : mail) {
    const std::string key = message.GetKey();
    if (key == "MEDIATED_MESSAGE_LOCAL" || key == "MEDIATED_MESSAGE_IN")
      dispatch(message, false, key == "MEDIATED_MESSAGE_IN");
    else if (key == "ACK_MESSAGE_LOCAL" || key == "ACK_MESSAGE_IN")
      dispatch(message, true, key == "ACK_MESSAGE_IN");
  }
  return true;
}

bool MediatorRouter::Iterate()
{
  AppCastingMOOSApp::Iterate();
  AppCastingMOOSApp::PostReport();
  return true;
}

bool MediatorRouter::buildReport()
{
  m_msgs << "Ownship: " << m_ownship << "\n"
         << "Valid: " << boolToString(m_valid)
         << "  Explicit relay: " << boolToString(m_allow_relay) << "\n\n"
         << std::left << std::setw(24) << "Local message posts" << m_local_messages << "\n"
         << std::setw(24) << "Local ACK posts" << m_local_acks << "\n"
         << std::setw(24) << "Rejected" << m_rejected << "\n\n"
         << std::setw(16) << "Destination" << std::setw(18) << "Message posts" << "ACK posts\n";
  for (const auto &peer : m_peers)
    m_msgs << std::setw(16) << peer << std::setw(18) << m_message_posts[peer] << m_ack_posts[peer]
           << "\n";
  m_msgs << "\nCounts are local MOOS posts, not remote delivery receipts.\n";
  return true;
}
