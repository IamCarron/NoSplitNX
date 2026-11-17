# Generation 3 Evolved widescreen

`gba/wide-patches/gen3evolved-wide.ips` and `.safety` add widescreen to Generation 3 Evolved v1.0.22. Other releases play as normal.

G3E is built on pokeemerald-expansion, not FireRed's code, so this patch does what FireRed's patch does in G3E's own code:

- asks for widescreen on the overworld and in battles
- wider maps, with a new camera to fill them
- characters and weather across the full width

**Tested:** walking (matches FireRed's widescreen exactly), 3:2 mode, menus, saving, battles, Surf, Fly, weather, and Fulldex picking the patch only for v1.0.22.

**Not tested yet:** cycling, diving, long sessions, time-of-day events.

The source and a tool to make the patch for new releases: <https://github.com/Saiborg23/g3e-fulldex-widescreen>
