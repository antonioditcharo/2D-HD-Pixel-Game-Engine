# Sources

All research was carried out in **November 2026**. Each source has an ID (`[S#]`) used
throughout the documents.

**How each source was verified**
- **Fetched:** the page itself was retrieved and read.
- **Indexed:** the page content was confirmed through a web-search index, because the site
  blocked direct retrieval or it was not fetched. The URL is the one the index reported.
- **Third-party:** a secondary source, used only where no primary source was available. The
  documents flag claims that rest on it.

Prices, versions and licence terms change. Re-verify anything before quoting it publicly.

Sources are grouped by topic, so IDs are not in strict numeric order. Search the page for
the ID (for example `S41`).

---

## HD-2D titles, techniques and shipped-game case studies

| ID | Source | Used for | Verification |
|---|---|---|---|
| S1 | Wikipedia, "HD-2D": https://en.wikipedia.org/wiki/HD-2D | Definition; defining techniques; title list; trademark summary; similar non-Square Enix games | Fetched |
| S2 | Epic Games, "Octopath Traveler's HD-2D art style and story make for a JRPG dream come true" (2019): https://www.unrealengine.com/spotlights/octopath-traveler-s-hd-2d-art-style-and-story-make-for-a-jrpg-dream-come-true | Octopath on UE4; team of up to six programmers relying on UE4's tools | Indexed (direct fetch returned 403) |
| S3 | Epic Games, "Octopath Traveler II builds a bigger, bolder world in its stunning HD-2D style": https://www.unrealengine.com/en-us/developer-interviews/octopath-traveler-ii-builds-a-bigger-bolder-world-in-its-stunning-hd-2d-style | High-resolution art that still reads as pixel art; day/night lighting; camera-rotation limits | Indexed (direct fetch returned 403) |
| S4 | Siliconera, "Project Octopath Traveler Developers Answer How The Project Started, And The Troubles In Developing HD-2D": https://www.siliconera.com/project-octopath-traveler-developers-answer-project-started-troubles-developing-hd-2d/ | Depth, resolution and saturation lessons; water; tile density | Fetched |
| S5 | Wikipedia, "Octopath Traveler II": https://en.wikipedia.org/wiki/Octopath_Traveler_II | Release date 24 Feb 2023; UE4; developers | Fetched |
| S6 | Wikipedia, "Triangle Strategy": https://en.wikipedia.org/wiki/Triangle_Strategy | Release 4 Mar 2022; UE4; Square Enix and Artdink | Fetched |
| S7 | Wikipedia, "Live A Live": https://en.wikipedia.org/wiki/Live_A_Live | Remake release 22 Jul 2022; UE4; Historia | Fetched |
| S8 | Square Enix, "DRAGON QUEST I & II HD-2D Remake set to launch on Oct. 30": https://www.square-enix.com/asia/newsportal/ja/topics/dragon-quest-i-ii-hd-2d/post03.html | Release 30 Oct 2025 (Steam 31 Oct) | Indexed |
| S9 | Square Enix, Octopath Traveler 0 news: https://www.square-enix.com/asia/newsportal/en/topics/octopathtraveler0/post01.html | Release 4 Dec 2025 (Steam 5 Dec) | Indexed |
| S10 | GIGAZINE, HD-2D Dragon Quest III release date: https://gigazine.net/gsc_news/en/20240618-hd-2d-dragon-quest-3 | Release 14 Nov 2024 | Fetched |
| S11 | Game Informer, "Square Enix Trademarks Octopath Traveler's HD-2D Phrase In Europe" (21 Jan 2019): https://www.gameinformer.com/2019/01/21/square-enix-trademarks-octopath-travelers-hd-2d-phrase-in-europe | Trademark filing | Indexed |
| S12 | Gematsu, "Square Enix trademarks HD-2D in Europe": https://gematsu.com/2019/01/square-enix-trademarks-hd-2d-in-europe-furyu-trademarks-heroland-in-the-us-and-europe | Trademark filing | Indexed |
| S13 | Markinton trademark database, "HD-2D" serial 88279578: https://markinton.com/trademark/hd-2d-88279578 | US registration 6040011 (2020) | Third-party; **not checked against USPTO** |
| S14 | Sabotage Studio, Sea of Stars press kit: https://sabotagestudio.com/presskits/sea-of-stars | "Custom-made render pipeline" for full dynamic lighting | Fetched |
| S15 | MobileSyrup, Sea of Stars interview with Thierry Boulanger (11 Sep 2023): https://mobilesyrup.com/2023/09/11/sea-of-stars-level-design-sabotage-studio-thierry-boulanger-interview/ | One engineer, about five years, on dynamic lighting; assets "answer to the technical needs" | Fetched |
| S16 | Kotaku, Eiyuden Chronicle graphics article: https://kotaku.com/suikoden-spiritual-successor-s-graphics-really-pop-1847088614 | Eiyuden Chronicle uses Unity | Indexed |
| S17 | Thomas Vasseur, "Art Design Deep Dive: Using a 3D pipeline for 2D animation in Dead Cells", Game Developer (originally Gamasutra), Jan 2018. Mirror: https://www.jikguard.com/news/art-design-deep-dive-using-a-3d-pipeline-for-2d-animation-in-dead-cells | 3ds Max → FBX → private batch tool → pixel frames | Indexed via mirror; canonical URL not retrieved |
| S18 | 80.lv, "Case Study: Dead Cells Character Art Pipeline": https://80.lv/articles/case-study-dead-cells-character-art-pipeline/ | Pipeline coverage; normal-mapped sprites demonstration | Fetched |
| S19 | ProPixelizer docs, "Eliminate pixel creep": https://propixelizer.github.io/docs/usage/eliminate-pixel-creep | Pixel creep definition; snapping; "only ever be solved for orthographic projections" | Fetched |
| S20 | D. Ebert, "Texel Splatting: Perspective-Stable 3D Pixel Art", arXiv:2603.14587 (15 Mar 2026): https://arxiv.org/abs/2603.14587 | Perspective-stable pixel-art research; disocclusion trade-off | Fetched |
| S83 | R. D. Moreira, F. Coutinho, L. Chaimowicz, "Analysis and Compilation of Normal Map Generation Techniques for Pixel Art", SBGames 2022, arXiv:2212.09692: https://arxiv.org/abs/2212.09692 | 3D normal-map methods may give poor results on pixel art | Fetched |
| S84 | MCV/Develop, "When We Made… Dead Cells": https://mcvuk.com/development-news/when-we-made-dead-cells/ | Dead Cells built with Haxe and the Heaps framework | Fetched |

## Engines and game makers

| ID | Source | Used for | Verification |
|---|---|---|---|
| S21 | Godot Engine, 4.6 release page: https://godotengine.org/releases/4.6/ | D3D12 default on Windows; Jolt default; IK; SSR; LibGodot (27 Jan 2026) | Indexed |
| S22 | Godot Engine, "Maintenance release: Godot 4.7.1" (14 Jul 2026): https://godotengine.org/article/maintenance-release-godot-4-7-1/ | Godot 4.7 stable in mid-June 2026 | Fetched |
| S23 | Godot Engine, "Godot 4 will discontinue visual scripting": https://godotengine.org/article/godot-4-will-discontinue-visual-scripting | 0.5% usage; lack of high-level building blocks | Fetched |
| S24 | Godot Engine, 4.5 release page: https://godotengine.org/releases/4.5/ | AccessKit screen-reader support (partial in editor); i18n live preview | Indexed |
| S93 | Godot Engine licence: https://github.com/godotengine/godot/blob/master/LICENSE.txt | Godot is MIT-licensed | Fetched |
| S25 | Unity, "Unity is canceling the Runtime Fee": https://unity.com/blog/unity-is-canceling-the-runtime-fee | Fee cancelled 12 Sep 2024; Personal cap US$200k; Pro US$2,200 from 1 Jan 2025 | Fetched |
| S26 | Unity 6 Manual, URP 2D (Renderer 2D, lights, Shadow Caster 2D, Pixel Perfect Camera): https://docs.unity3d.com/6/Documentation/Manual/2d-urp-landing.html · https://docs.unity3d.com/Manual/urp/create-light-2d.html · https://docs.unity3d.com/Manual/urp/ShadowCaster2D.html | 2D lights with normal and mask maps; Shadow Caster 2D; Pixel Perfect Camera | Indexed |
| S27 | Epic Games, Unreal Engine licensing options: https://www.unrealengine.com/license | 5% royalty above US$1M lifetime gross; Epic Games Store revenue exempt; 3.5% "Launch Everywhere with Epic" rate | Indexed (direct fetch returned 403) |
| S28 | Critical Failure Studio, PaperZD on itch.io: https://criticalfailure-studio.itch.io/paperzd | 2D animation state machines for Unreal; US$44.99 | Indexed |
| S29 | Game World Observer, GameMaker pricing (22 Nov 2023): https://gameworldobserver.com/2023/11/22/gamemaker-new-pricing-free-non-commercial-use-one-time-pro-license · official: https://gamemaker.io/get | Free non-commercial; US$99.99 one-time Professional; Enterprise for consoles | Indexed |
| S30 | PocketGamer.biz, "GameMaker makes GMRT free for commercial use from 2027": https://www.pocketgamer.biz/gamemaker-makes-gmrt-free-for-commercial-use-from-2027/ | GMRT commercial use free from 1 Jan 2027 | Indexed |
| S85 | GameMaker Manual, GML Visual overview: https://manual.gamemaker.io/lts/en/Drag_And_Drop/Drag_And_Drop_Overview/DnD_Overview.htm | GML Visual: actions dragged from a toolbox and chained | Indexed (direct fetch returned 403) |
| S31 | GDevelop repository: https://github.com/4ian/GDevelop | MIT licence; PixiJS/Three.js; React/Electron; Jolt | Fetched |
| S32 | GDevelop wiki, Events: https://wiki.gdevelop.io/gdevelop5/events/ | Conditions, actions, sub-events, picking, event types, TimeDelta | Fetched |
| S33 | GDevelop, "Game makers" page: https://gdevelop.io/game-makers | "More than 130 ready made behaviors and extensions"; AI agent; 2D and 3D | Fetched |
| S34 | Construct, "Buy Construct" (plans and free edition limits): https://www.construct.net/en/make-games/buy-construct | Free edition limits | Fetched |
| S35 | Construct release notes: https://www.construct.net/en/make-games/releases (r268: 3D camera; r469 beta, Jan 2026: built-in glTF/GLB 3D model plugin) | Construct 3D features | Indexed (direct fetch returned 403) |
| S36 | RPG Maker MZ Help, event triggers: https://rpgmakerofficial.com/product/MZ_help-en/01_09_03.html | Event page conditions and triggers | Fetched |
| S37 | RPG Maker Web blog, "Event priorities and triggers": https://rpgmakerweb.com/blog/event-priorities-and-triggers | Parallel vs Autorun | Fetched |
| S90 | RPG Maker Web blog, "Using Plugins in MZ": https://rpgmakerweb.com/blog/using-plugins-in-mz · MZ Help, "Using Official Plugins": https://rpgmakerofficial.com/product/MZ_help-en/01_11_05.html | JavaScript plugins in `js/plugins`, managed via the Plugin Manager | Indexed |
| S38 | AUTOMATON (JP), RPG Maker Unite release report: https://automaton-media.com/articles/newsjp/20230425-245257/ | Release 27 Apr 2023; US$99.99; runs inside the Unity editor; no-code events; Outline Editor; Full HD; add-ons | Fetched |
| S39 | Steam, RPG Maker Unite: https://store.steampowered.com/app/1650950 | "No longer available on the Steam store" | Indexed |
| S86 | Steam Community, RPG Maker Unite discussion thread: https://steamcommunity.com/app/1650950/discussions/0/3820784984102038111 | Mixed user sentiment (anecdotal) | Indexed |
| S40 | AUTOMATON, "RPG Maker devs to release Action Game Maker, a Godot-based toolkit that doesn't require programming": https://automaton-media.com/en/news/rpg-maker-devs-to-release-action-game-maker-a-godot-based-toolkit-that-doesnt-require-programming/ · Godot showcase: https://godotengine.org/showcase/action-game-maker/ | Godot-based; no programming required | Fetched (headline only; body truncated) |
| S41 | Steam, ACTION GAME MAKER: https://store.steampowered.com/app/2987180/ | Release 16 Jun 2025; "Godot Engine 4.7.0"; states as boxes connected by arrows; custom actions and conditions in GDScript; no programming required | Fetched |
| S42 | Pixel Game Maker MV manual, features: https://rpgmakerofficial.com/product/act/en/manual/01_02.html | Flowchart-style visual scripting with no programming; JavaScript extensions; physics; local play for up to 4 | Fetched |
| S87 | Gematsu, "Action Game Maker MV coming west as Pixel Game Maker MV" (May 2018): https://gematsu.com/2018/05/action-game-maker-mv-coming-west-pixel-game-maker-mv-first-details-screenshots | Pixel Game Maker MV's Japanese title was Action Game Maker MV (same series as ACTION GAME MAKER) | Indexed (headline) |
| S88 | Construct 3 documentation: Events https://www.construct.net/en/make-games/manuals/construct-3/project-primitives/events · Functions https://www.construct.net/en/make-games/manuals/construct-3/project-primitives/events/functions · Custom actions https://www.construct.net/en/make-games/manuals/construct-3/project-primitives/events/custom-actions · Addon SDK (behaviors) https://www.construct.net/en/make-games/manuals/addon-sdk | Event sheets; behaviors add actions, conditions and expressions; functions and custom actions | Indexed (direct fetch returned 403) |
| S89 | GameFromScratch, "Pixel Game Maker MV Released" (21 Sep 2019): https://gamefromscratch.com/pixel-game-maker-mv-released/ | Full Steam release after early access | Fetched |
| S43 | RPG in a Box FAQ: https://rpginabox.com/faq | Godot-based standalone; Bauxite; visual scripts; exports | Indexed |
| S44 | SmileBoom, SMILE GAME BUILDER: https://smileboom.com/en/product/sgb-info | 3D-map RPG maker; flowchart or line events; 1,000+ materials | Indexed |

## Content tools

| ID | Source | Used for | Verification |
|---|---|---|---|
| S45 | Wikipedia, "Aseprite": https://en.wikipedia.org/wiki/Aseprite | EULA since v1.1.8 (Aug 2016); LibreSprite fork; features | Indexed |
| S46 | Aseprite file format specification: https://github.com/aseprite/aseprite/blob/main/docs/ase-file-specs.md | Layers, cels, tags, palettes, slices, tilesets, user data | Fetched |
| S47 | Pixelorama repository: https://github.com/Orama-Interactive/Pixelorama | MIT; built with Godot; animation, tilemaps, 3D layers, exports | Fetched |
| S48 | Laigter, "Laigter is now open source": https://azagaya.itch.io/laigter/devlog/89223/laigter-is-now-open-source | GPLv3; normal, specular, occlusion and parallax maps | Indexed |
| S49 | N4G, "Sprite DLight – instant normal maps for 2D graphics": https://n4g.com/news/1626187/sprite-dlight-instant-normal-maps-for-2d-graphics | Normal, depth, AO and specular generation | Indexed |
| S50 | Game Developer, "Game Design Deep Dive: Visualizing Cryptark's 2D sci-fi world": https://gamedeveloper.com/design/game-design-deep-dive-visualizing-i-cryptark-s-i-2d-sci-fi-world | Generated normals looking "plastic wrap" | Indexed |
| S51 | Steam, Crocotile 3D: https://store.steampowered.com/app/1244040/Crocotile_3D | Tile-based 3D modelling with pixel-art textures; US$29.99 | Indexed |
| S52 | Digital Production, "Crocotile 3D 2.4.5" (2 Dec 2024): https://digitalproduction.com/2024/12/02/crocotile-3d-2-4-5-tile-based-3d-modeling-tool-gets-new-features/ | Rigging and skinning added; exports | Indexed |
| S53 | LDtk on itch.io: https://deepnight.itch.io/ldtk · repository: https://github.com/deepnight/ldtk | MIT; auto-layer rules; entity fields and references; exports | Indexed |
| S54 | jfxr repository: https://github.com/ttencate/jfxr | BSD-3; sounds belong to the user | Fetched |
| S55 | Audiokinetic blog, "Free Wwise for indie developers": https://blog.audiokinetic.com/free-wwise-for-indie-developers/ | Free Indie licence under a US$250K budget; no asset limit since Apr 2022 | Indexed |
| S56 | ink repository: https://github.com/inkle/ink | MIT; C# runtime; inkjs | Fetched |
| S57 | Yarn Spinner repository: https://github.com/YarnSpinnerTool/YarnSpinner | MIT; engine-agnostic core; Unity package | Fetched |
| S58 | Steamworks SDK documentation: https://partner.steamgames.com/doc/sdk | Feature list; SDK required only for uploading | Fetched |
| S91 | Valve, Steamworks SDK Access Agreement: https://partner.steamgames.com/documentation/sdk_access_agreement | Licence to "use and locally reproduce the SDK"; distribution limited to the `redistributable_bin` folder | Fetched |
| S92 | Steamworks Documentation, "Distributing Open Source Applications on Steam": https://partner.steamgames.com/doc/sdk/uploading/distributing_opensource | MIT, BSD and Apache 2.0 listed as compatible with the SDK; copyleft licences such as the GPL are problematic | Fetched |

## No-code paradigms

| ID | Source | Used for | Verification |
|---|---|---|---|
| S59 | Epic Developer Community, "Direct Event Binding in Unreal Editor for Fortnite": https://dev.epicgames.com/documentation/en-us/uefn/direct-event-binding-in-unreal-editor-for-fortnite | Event → function wiring between devices | Fetched |
| S60 | Fortnite, "The transition to Direct Event Binding to occur in Fortnite v25.00": https://www.fortnite.com/news/the-transition-to-direct-event-binding-to-occur-in-fortnite-v25-00 | Replacement of channels; default for Creative islands | Indexed |
| S61 | Wikipedia, "Game Builder Garage": https://en.wikipedia.org/wiki/Game_Builder_Garage | Nodon visual programming (2021) | Indexed |

## Name screening (queried 10 November 2026)

"Fetched (live query)" means the search was run against the site itself on the day, in a
browser, and the results were read directly.

| ID | Source | Used for | Verification |
|---|---|---|---|
| S94 | USPTO Trademark Search: https://tmsearch.uspto.gov/search/search-information | US wordmark searches for the five candidate names, one-word and two-word forms | Fetched (live query) |
| S95 | EUIPO eSearch plus: https://euipo.europa.eu/eSearch/ | EU trade mark searches for the five candidate names, one-word and two-word forms | Fetched (live query) |
| S96 | WIPO Global Brand Database: https://branddb.wipo.int/ | International marks | **Not queried:** an automated-access check sits in front of the search |
| S97 | Steam store search: https://store.steampowered.com/search/ | Games and software named like each candidate | Fetched (live query) |
| S98 | GitHub repository search (name matches) | Code projects named like each candidate | Fetched (live query) |
| S99 | Product Hunt, "Pixelwright": https://www.producthunt.com/products/pixelwright | Existing pixel-art studio for game art at pixelwright.app, launched 2026 | Fetched |
| S100 | David Steeman, "Building a TiltBox": https://www.steeman.be/posts/building-a-tiltbox/ · Cromwell Tools, bott "Perfo Tilt Box": https://cromwell.co.uk/shop/storage-handling-and-packaging/small-parts-storage-cabinet/perfo-tilt-box-polypropylene-grey-transparent-600x68x78mm-27-compartments/p/BOT4051034P | A hobby handheld console and storage boxes named TiltBox | Indexed |
| S101 | Lantern Studio: https://lantern-studio.com/about | A game studio using "Lantern" | Indexed |
| S102 | Justia Trademarks, GLIMMER STUDIOS: https://trademark.justia.com/976/37/glimmer-97637759.html · Glimmerjam LLC: https://trademark.justia.com/owners/glimmerjam-llc-3383077 | Nearby "Glimmer" marks for game and app software | Indexed; third-party directory |
| S103 | Registry RDAP lookups: Verisign (`.com`, `.net`) https://rdap.verisign.com/com/v1/domain/sparxel.com · Public Interest Registry (`.org`) https://rdap.publicinterestregistry.org/rdap/domain/sparxel.org · Google Registry (`.dev`, `.app`) https://pubapi.registry.google/rdap/domain/sparxel.dev · Identity Digital (`.io`) https://rdap.identitydigital.services/rdap/domain/sparxel.io | Registration status of glimmerbox and sparxel domains | Fetched (live query) |
| S104 | Wikipedia, "Sparx Systems": https://en.wikipedia.org/wiki/Sparx_Systems · Wikipedia, "Sparx* Studios": https://en.wikipedia.org/wiki/Sparx*_Studios | Similar names in software and games | Indexed |

## Technology stack

| ID | Source | Used for | Verification |
|---|---|---|---|
| S62 | SDL Wiki, GPU category: https://wiki.libsdl.org/CategoryGPU | SDL GPU API; D3D12, Vulkan and Metal drivers | Indexed |
| S63 | SDL Wiki, `SDL_CreateGPUComputePipeline`: https://wiki.libsdl.org/SDL3/SDL_CreateGPUComputePipeline | Compute pipelines; storage textures and buffers | Indexed |
| S64 | Moonside Games, "Introducing SDL_shadercross": https://moonside.games/posts/introducing-sdl-shadercross/ | HLSL/SPIR-V in, all backends out; DXC needed for runtime HLSL | Indexed |
| S65 | SDL releases: https://github.com/libsdl-org/SDL/releases | 3.4.x releases through Nov 2026 | Fetched |
| S66 | SDL licence: https://github.com/libsdl-org/SDL/blob/main/LICENSE.txt | zlib licence | Fetched |
| S67 | Box2D repository: https://github.com/erincatto/box2d | MIT; feature list | Fetched |
| S68 | Box2D blog, "Box2D 3.1" (Apr 2025): https://box2d.org/posts/2025/04/box2d-3.1 · v3.1.0 release notes: https://box2d.org/documentation/md_release__notes__v310.html | Continuous collision about twice as fast; new character mover; revised sensors | Indexed |
| S69 | Jolt Physics repository: https://github.com/jrouwe/JoltPhysics | MIT; determinism; character controllers; games using it | Fetched |
| S70 | flecs repository: https://github.com/SanderMertens/flecs | MIT; relationships; prefabs; reflection and JSON; explorer | Fetched |
| S71 | Luau repository: https://github.com/luau-lang/luau | MIT; sandboxing; gradual typing; users | Fetched |
| S105 | nlohmann/json repository: https://github.com/nlohmann/json (`LICENSE.MIT`, tag v3.12.0) | MIT; header-only JSON library used for project files | Fetched (the tagged source was cloned and its licence file read) |
| S72 | miniaudio repository: https://github.com/mackron/miniaudio | Public domain or MIT-0; WASAPI; node graph; decoders | Fetched |
| S73 | Dear ImGui repository: https://github.com/ocornut/imgui | MIT; not designed for end-user UI, accessibility or full i18n | Fetched |
| S74 | Qt 6 documentation, Licensing: https://doc.qt.io/qt-6/licensing.html | LGPLv3 option; GPLv3-only module list | Fetched |
| S75 | Qt 6 documentation, Accessibility: https://doc.qt.io/qt-6/accessible.html | Platform accessibility APIs for Widgets and Quick | Fetched |
| S76 | Qt Advanced Docking System: https://github.com/githubuser0xFFFF/Qt-Advanced-Docking-System | LGPL-2.1; Qt 6 support; users | Fetched |
| S77 | libgit2 repository: https://github.com/libgit2/libgit2 | GPLv2 with linking exception | Fetched |
| S78 | Tracy licence: https://github.com/wolfpld/tracy (LICENSE) | BSD-3; profiler description | Fetched |
| S79 | HarfBuzz COPYING: https://github.com/harfbuzz/harfbuzz/blob/main/COPYING | "Old MIT" licence | Fetched |
| S80 | FreeType licence page: https://freetype.org/license.html | FTL (BSD-style with credit) or GPLv2 | Fetched |
| S81 | Microsoft Learn, "Distribute your app and the WebView2 Runtime": https://learn.microsoft.com/microsoft-edge/webview2/concepts/distribution | Evergreen runtime preinstalled on Windows 11 | Indexed |
| S82 | Microsoft, Windows 10 end of support: https://www.microsoft.com/en-us/windows/end-of-support | Support ended 14 Oct 2025; consumer ESU through 12 Oct 2027 | Fetched |
