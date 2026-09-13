# M1 interface preview device test

This build has a native interface, not a connected YouTube client. No results,
account login, stream playback, SponsorBlock or DeArrow are implemented yet.
M0's accepted text-screen boot remains the hardware baseline until this passes.

1. Copy `tizentube_nx.nro` to `sdmc:/switch/TizenTube-NX/`.
2. Launch with title takeover (hold R while launching a game, then use hbmenu).
3. Confirm Home, Search, Subscriptions, Library and Settings appear in that order.
4. Move between all five sections using D-pad and stick; enter Search/Settings
   with A or Right. B and Left should return to the sidebar without exiting.
5. In handheld mode, tap sections and buttons. Check that selected pages change.
6. In Search, open the keyboard, enter text, confirm, then switch sections and
   return. The text should remain for this launch. Cancel a second edit and
   confirm the prior text remains. No results should be fabricated.
7. In Settings, toggle Show frame rate. Exit with +, relaunch and check persistence.
8. Test handheld/docked transitions and both Switch system themes. Check for
   clipped text, loss of focus, freezes and graphics corruption.
9. Exit with + from each section. If needed, the fixed boot-state record is at
   `sdmc:/switch/TizenTube-NX/logs/boot.log`; it contains no search text or tokens.

Report launch mode, firmware/Atmosphere versions, failing step and a photo for
any rendering failure. Also test applet mode separately; it is not yet accepted.
