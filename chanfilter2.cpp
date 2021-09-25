/*
 * Copyright (C) 2021 DannyAAM
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <znc/Modules.h>
#include <znc/IRCNetwork.h>
#include <znc/IRCSock.h>
#include <znc/Client.h>
#include <znc/Chan.h>
#include <znc/Nick.h>
#include <znc/version.h>

#if (VERSION_MAJOR < 1) || (VERSION_MAJOR == 1 && VERSION_MINOR < 7)
#error The chanfilter2 module requires ZNC version 1.7.0 or later.
#endif

struct ModeAndChannels
{
    CString mode;
    SCString channels;

    void FromString(const CString& data)
    {
        mode = data.Token(0);
        data.Token(1).Split(",", channels);
    }

    CString ToString()
    {
        return mode + " " + CString(",").Join(channels.begin(), channels.end());
    }
};

class CChanFilter2Mod : public CModule
{
public:
    MODCONSTRUCTOR(CChanFilter2Mod)
    {
        AddHelpCommand();
        AddCommand("AddClient", static_cast<CModCommand::ModCmdFunc>(&CChanFilter2Mod::OnAddClientCommand), "<identifier> [mode]", "Add a client, mode is either whitelist or blacklist (default to blacklist.)");
        AddCommand("DelClient", static_cast<CModCommand::ModCmdFunc>(&CChanFilter2Mod::OnDelClientCommand), "<identifier>", "Delete a client.");
        AddCommand("ListClients", static_cast<CModCommand::ModCmdFunc>(&CChanFilter2Mod::OnListClientsCommand), "", "List known clients and their mode and channel list.");
        AddCommand("ListChans", static_cast<CModCommand::ModCmdFunc>(&CChanFilter2Mod::OnListChansCommand), "[client]", "List all channels of a client.");
        AddCommand("RestoreChans", static_cast<CModCommand::ModCmdFunc>(&CChanFilter2Mod::OnRestoreChansCommand), "[client]", "Restore the hidden channels of a client (for balcklist mode only.)");
        AddCommand("HideChans", static_cast<CModCommand::ModCmdFunc>(&CChanFilter2Mod::OnHideChansCommand), "[client]", "Hide all channels of a client.");
    }

    bool OnLoad(const CString& sArgs, CString& sMessage) override;

    void OnPartDetachCommand(const CString& line);
    void OnAddClientCommand(const CString& line);
    void OnDelClientCommand(const CString& line);
    void OnListClientsCommand(const CString& line);
    void OnListChansCommand(const CString& line);
    void OnRestoreChansCommand(const CString& line);
    void OnHideChansCommand(const CString& line);

    virtual EModRet OnUserJoinMessage(CJoinMessage& message) override;
    virtual EModRet OnUserPartMessage(CPartMessage& message) override;
    virtual EModRet OnSendToClientMessage(CMessage& message) override;

private:
    bool partdetach;

    ModeAndChannels GetModeAndChannels(const CString& identifier) const;
    bool IsChannelVisible(const CString& identifier, const CString& channel) const;
    void SetChannelVisible(const CString& identifier, const CString& channel, bool visible);

    bool AddClient(const CString& identifier, const CString& mode);
    bool DelClient(const CString& identifier);
    bool HasClient(const CString& identifier);
};

bool CChanFilter2Mod::OnLoad(const CString& arg, CString& message)
{
    partdetach = arg.Find("partdetach") != CString::npos;
    return true;
}

void CChanFilter2Mod::OnAddClientCommand(const CString& line)
{
    const CString identifier = line.Token(1);
    CString mode = line.Token(2).AsLower();
    if (identifier.empty()) {
        PutModule("Usage: AddClient <identifier> [mode]");
        return;
    }
    if (HasClient(identifier)) {
        PutModule("Client already exists: " + identifier);
        return;
    }
    if (mode.empty())
        mode = "blacklist";
    if (!mode.Equals("blacklist") && !mode.Equals("whitelist")) {
        PutModule("Mode must be either blacklist or whitelist");
        return;
    }
    AddClient(identifier, mode.AsLower());
    PutModule("Client added: " + identifier);
}

void CChanFilter2Mod::OnDelClientCommand(const CString& line)
{
    const CString identifier = line.Token(1);
    if (identifier.empty()) {
        PutModule("Usage: DelClient <identifier>");
        return;
    }
    if (!HasClient(identifier)) {
        PutModule("Unknown client: " + identifier);
        return;
    }
    DelClient(identifier);
    PutModule("Client removed: " + identifier);
}

void CChanFilter2Mod::OnListClientsCommand(const CString& line)
{
    const CString current = GetClient()->GetIdentifier();

    CTable table;
    table.AddColumn("Client");
    table.AddColumn("Connected");
    table.AddColumn("Mode");
    table.AddColumn("Channel list");
    for (MCString::iterator it = BeginNV(); it != EndNV(); ++it) {
        if (it->first.StartsWith(",")) // special prefix for non-channel settings (invalid char for channel name)
            continue;
        table.AddRow();
        if (it->first == current)
            table.SetCell("Client",  "*" + it->first);
        else
            table.SetCell("Client",  it->first);
        table.SetCell("Connected", CString(!GetNetwork()->FindClients(it->first).empty()));
        table.SetCell("Mode", it->second.Token(0));
        table.SetCell("Channel list", it->second.Token(1).Ellipsize(128));
    }

    if (table.empty())
        PutModule("No identified clients");
    else
        PutModule(table);
}

void CChanFilter2Mod::OnListChansCommand(const CString& line)
{
    CString identifier = line.Token(1);
    if (identifier.empty())
        identifier = GetClient()->GetIdentifier();

    if (identifier.empty()) {
        PutModule("Unidentified client");
        return;
    }

    if (!HasClient(identifier)) {
        PutModule("Unknown client: " + identifier);
        return;
    }

    CTable table;
    table.AddColumn("Client");
    table.AddColumn("Channel");
    table.AddColumn("Status");

    for (CChan* channel : GetNetwork()->GetChans()) {
        table.AddRow();
        table.SetCell("Client", identifier);
        table.SetCell("Channel", channel->GetName());
        if (channel->IsDisabled())
            table.SetCell("Status", "Disabled");
        else if (channel->IsDetached())
            table.SetCell("Status", "Detached");
        else if (IsChannelVisible(identifier, channel->GetName()))
            table.SetCell("Status", "Visible");
        else
            table.SetCell("Status", "Hidden");
    }

    PutModule(table);
}

void CChanFilter2Mod::OnRestoreChansCommand(const CString& line)
{
    CString identifier = line.Token(1);
    if (identifier.empty())
        identifier = GetClient()->GetIdentifier();

    if (identifier.empty()) {
        PutModule("Unidentified client");
        return;
    }

    if (!HasClient(identifier)) {
        PutModule("Unknown client: " + identifier);
        return;
    }

    const ModeAndChannels info = GetModeAndChannels(identifier);
    if (!info.mode.Equals("blacklist")) {
        PutModule("Not blacklist mode");
        return;
    }
    if (info.channels.empty()) {
        PutModule("No hidden channels");
        return;
    }

    unsigned int count = 0;
    CIRCNetwork* network = GetNetwork();
    for (const CString& channelName : info.channels) {
        SetChannelVisible(identifier, channelName, true);
        CChan* channel = network->FindChan(channelName);
        if (channel) {
            for (CClient* client : network->FindClients(identifier))
                channel->AttachUser(client);
            ++count;
        }
    }
    PutModule("Restored " + CString(count) + " channels");
}

void CChanFilter2Mod::OnHideChansCommand(const CString& line)
{
    CString identifier = line.Token(1);
    if (identifier.empty())
        identifier = GetClient()->GetIdentifier();

    if (identifier.empty()) {
        PutModule("Unidentified client");
        return;
    }

    if (!HasClient(identifier)) {
        PutModule("Unknown client: " + identifier);
        return;
    }

    unsigned int count = 0;
    CIRCNetwork* network = GetNetwork();
    for (CChan* channel : network->GetChans()) {
        const CString channelName = channel->GetName();
        if (channel->IsOn() && IsChannelVisible(identifier, channelName)) {
            SetChannelVisible(identifier, channelName, false);
            for (CClient* client : network->FindClients(identifier)) {
                // use Write() instead of PutClient() to bypass OnSendToClient()
                client->Write(":" + client->GetNickMask() + " PART " + channelName + "\r\n");
            }
            ++count;
        }
    }
    PutModule("Hid " + CString(count) + " channels");
}

CModule::EModRet CChanFilter2Mod::OnUserJoinMessage(CJoinMessage& message) {
    // a join command from an identified client either
    // - restores a hidden channel and is filtered out
    // - is let through so ZNC joins the channel
    const CString identifier = GetClient()->GetIdentifier();
    if (HasClient(identifier)) {
        const CString channelName = message.GetTarget();
        SetChannelVisible(identifier, channelName, true);
        CIRCNetwork* network = GetNetwork();
        CChan* channel = network->FindChan(channelName);
        if (channel) {
            for (CClient* client : network->FindClients(identifier))
                channel->AttachUser(client);
            return HALT;
        }
    }
    return CONTINUE;
}

CModule::EModRet CChanFilter2Mod::OnUserPartMessage(CPartMessage& message) {
    const CString channelName = message.GetTarget();
    CIRCNetwork* network = GetNetwork();
    CChan* channel = network->FindChan(channelName);
    if (channel) {
        // a part command from an identified client either
        // - hides a visible channel and is filtered out
        // - is let through so ZNC parts the channel
        const CString identifier = GetClient()->GetIdentifier();
        if (HasClient(identifier) && IsChannelVisible(identifier, channelName)) {
            SetChannelVisible(identifier, channelName, false);
            for (CClient* client : network->FindClients(identifier)) {
                // use Write() instead of PutClient() to bypass OnSendToClient()
                client->Write(":" + client->GetNickMask() + " PART " + channelName + "\r\n");
            }
            return HALT;
        }
        // partdetach
        if (partdetach) {
            if (!channel->IsDetached() && channel->InConfig()) {
                CString reason = message.GetReason();
                if (reason.Token(0).CaseCmp("force") == 0) {
                     message.SetReason(reason.Token(1, true));
                } else {
                    channel->DetachUser();
                    return HALTCORE;
                }
            }
        }
    }
    return CONTINUE;
}

CModule::EModRet CChanFilter2Mod::OnSendToClientMessage(CMessage& message)
{
    EModRet result = CONTINUE;

    CClient* client = message.GetClient();
    const CString identifier = client->GetIdentifier();
    CIRCNetwork* network = GetNetwork();

    if (network && HasClient(identifier)) {
        const CNick& nick = message.GetNick();
        const CString& command = message.GetCommand();

        // identify the channel token from (possibly) channel specific messages
        CString channelName;
        switch (message.GetType()) {
            case CMessage::Type::Text:
            case CMessage::Type::CTCP:
            case CMessage::Type::Action:
            case CMessage::Type::Notice:
            case CMessage::Type::Join:
            case CMessage::Type::Part:
            case CMessage::Type::Mode:
            case CMessage::Type::Kick:
            case CMessage::Type::Topic:
                channelName = message.GetParam(0);
                break;
            case CMessage::Type::Numeric: {
                switch (command.ToUInt()) {
                    case 332:  // RPL_TOPIC
                    case 333:  // RPL_TOPICWHOTIME
                    case 366:  // RPL_ENDOFNAMES
                        channelName = message.GetParam(1);
                        break;
                    case 353:  // RPL_NAMREPLY
                        channelName = message.GetParam(2);
                        break;
                    case 322:  // RPL_LIST
                    default:
                        return CONTINUE;
                }
                break;
            }
            default:
                return CONTINUE;
        }

        // remove status prefix (#1)
        CIRCSock* sock = client->GetIRCSock();
        if (sock)
            channelName.TrimLeft(sock->GetISupport("STATUSMSG", ""));

        // filter out channel specific messages for hidden channels
        if (network->IsChan(channelName) && !IsChannelVisible(identifier, channelName)) {
            result = HALTCORE;
        }

        // for a server PART reply, clear the visibility status after the PART was relayed.
        if (command.Equals("PART") && nick.GetNick().Equals(client->GetNick()))
            SetChannelVisible(identifier, channelName, false);
    }
    return result;
}

ModeAndChannels CChanFilter2Mod::GetModeAndChannels(const CString& identifier) const
{
    ModeAndChannels info;
    info.FromString(GetNV(identifier));
    return info;
}

bool CChanFilter2Mod::IsChannelVisible(const CString& identifier, const CString& channel) const
{
    const ModeAndChannels info = GetModeAndChannels(identifier);
    if (info.mode.Equals("blacklist"))
        return info.channels.find(channel.AsLower()) == info.channels.end();
    else if (info.mode.Equals("whitelist"))
        return info.channels.find(channel.AsLower()) != info.channels.end();
    return true;
}

void CChanFilter2Mod::SetChannelVisible(const CString& identifier, const CString& channel, bool visible)
{
    if (!identifier.empty()) {
        ModeAndChannels info = GetModeAndChannels(identifier);
        if (info.mode.Equals("blacklist")) {
            if (visible)
                info.channels.erase(channel.AsLower());
            else
                info.channels.insert(channel.AsLower());
        } else if (info.mode.Equals("whitelist")) {
            if (visible)
                info.channels.insert(channel.AsLower());
            else
                info.channels.erase(channel.AsLower());
        }
        SetNV(identifier, info.ToString());
    }
}

bool CChanFilter2Mod::AddClient(const CString& identifier, const CString& mode)
{

    return SetNV(identifier, mode + " ");
}

bool CChanFilter2Mod::DelClient(const CString& identifier)
{
    return DelNV(identifier);
}

bool CChanFilter2Mod::HasClient(const CString& identifier)
{
    return !identifier.empty() && FindNV(identifier) != EndNV();
}

NETWORKMODULEDEFS(CChanFilter2Mod, "A channel filter for identified clients including partdetach")
