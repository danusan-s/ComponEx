MultiplayerDemo
===============

Walk around a flat ground with WASD, mouse to look, Space to jump, Shift to
sprint, Esc to free the mouse. Other players show up as coloured blocks (the
white block on the front is the way they face).

Same machine / same network
---------------------------
Run play-lan.bat twice. The first copy hosts, the second joins it.
From another PC on your network: MultiplayerDemo.exe <host-ip>:7777

Over the internet (STUN + invite codes)
---------------------------------------
No server of ours is involved. Each game asks a public STUN server (Google,
Cloudflare) what its address looks like from outside, and shows it as an invite
code such as 0K10-R218-2E45-0. You swap the codes yourselves (chat, call...).

  Host:   run play-internet-host.bat. Send your friend the code it prints.
  Friend: run play-internet-join.bat, enter the host's code, then send the
          host the code it prints.
  Host:   paste the friend's code into the host's console window (click the
          black window, right-click to paste) and press Enter.

Do the last step within about a minute of the friend starting.

Notes
-----
* Windows Firewall will ask to allow the game the first time: allow it.
* It works through most home routers, but not all. If a game says its NAT is
  SYMMETRIC (common on mobile data and some office/campus networks) it cannot
  connect this way.
* Two players on the SAME home network cannot reach each other through their
  public addresses; use the LAN option for that.
* The STUN servers see your IP address; that is how this works.
