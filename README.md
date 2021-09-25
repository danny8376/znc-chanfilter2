znc-chanfilter2
===================================

This module is similar to [chanfilter](https://wiki.znc.in/Chanfilter) and modified from there.

There's blacklist mode, which list channels you want to hide, and whitelist mode, which list channels you only want to show.
This module also implements function of [partdetach](https://wiki.znc.in/Partdetach).


Build
-----------------------------------

Build it with

    $ znc-buildmod chanfilter2.cpp

Install
-----------------------------------

Just like any other znc modules.

Usage
-----------------------------------

When a client connects to ZNC for the first time, all channels are joined for blacklist mode, and no channel is joined for whitelist mode.
The list of channels is automatically updated when the client joins and parts channels.
Next time the identified client connects, it joins the channels it had visible from the last session.
Also, for newly joined channels, it'll visible in all clients with blacklist mode.

This only happens to clients that are identified and added (see below).

Note that because this module intercepts /part, joining a new channel and then parting it in an identified client will leave it attached in ZNC (and other clients).
Typing /part #channel another time will pass the action through to ZNC and part the channel on the network.

However, if you enable partdetach function, 2nd part command will just detach the channel for all clients.
Type 3rd time to actually part the channel.
For unidentified clients, it just behave like [partdetach](https://wiki.znc.in/Partdetach).

Identifiers
-----------------------------------
ZNC supports passing a client identifier in the password:

`username@identifier/network:password`

or in the username:

`username@identifier/network`


Arguments
-----------------------------------
Pass `partdetach` to enable partdetach function.

Commands
-----------------------------------
Add a client (mode default to blacklist):

`/msg *chanfilter AddClient <identifier> [mode]`

Delete a client:

`/msg *chanfilter DelClient <identifier>`

List all channels of a client or current client:

`/msg *chanfilter ListChans [client]`

List known clients and their hidden channels:

`/msg *chanfilter ListClients`

Restore the hidden channels of a client or current client (only works for blacklist mode client):

`/msg *chanfilter RestoreChans [client]`

Hide all channels for a client or current client:

`/msg *chanfilter HideChans [client]`

