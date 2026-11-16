# Guided modeling: detail levels

Every guided template (fence, architecture, vehicles, characters) has a shared **Detail** setting: Low, Medium, High, Ultra. It is
stored in the recipe (`param detail 0..3`); a recipe without it (older files) is Medium, which is exactly the original shape.

| Level | What changes |
|---|---|
| Low | Curves use half the segments (round posts, wheels, columns, balls are blocky); everything else as Medium. Flat-shaded. |
| Medium | The standard shapes (the default, and what every older model is). |
| High | Twice the segments on curves; every box gets chamfered long edges; extra trim (wall cornices, wheel lug nuts); smooth shading in the editor. |
| Ultra | Four times the segments; box edges rounded (three steps per corner); a second ledge under cornices, eight lug nuts; smooth shading. |

Rules (tested in `tests/test_guided_detail.cpp`, ctest `guided_detail`): the same recipe always gives the same mesh at a level;
every part at every level is a closed, valid, finite solid with outward faces and no two parts share a corner; the triangle
count never goes down with the level and each step from Medium up adds at least 20 percent; switching level goes through the
normal rebuild, so part swaps stay, your own added geometry is copied through untouched and a part you edited by hand is asked about
(keep / discard / convert). Smooth shading (High and Ultra) is applied by the Guided page to the model's mesh, not stored in the recipe.

Known limit: a wall panel (one shell with holes) and the box-like parts cannot get fewer polygons at Low than a box has, so a
box-only design (for example the picket fence) has the same triangle count at Low and Medium.

### Fine detail added in this round (Ultra triangles, before and after)

Cars at High and Ultra now carry purposeful small parts instead of subdivided arches: door handles and panel seams, window rubber and door pillar, wipers, grille bars, lamp lenses and tail-light ribs, mirror glass, number plate and frame, exhaust tips, underbody parts (gearbox tunnel, tank, silencer, propeller shaft), brake discs with calipers and ridges on the tyre walls. Buildings get rows of roof tiles on every slope, gutters and a down pipe. Humans get buttons, a placket, pockets and a hem band on the top. All of them are parts of their own (slot "Fine fittings" / "Tiles and gutters") that can be removed. Cars and roofs are no longer subdivided at Ultra (their outlines are already smooth or have hard edges that subdivision would melt).

| Model | Ultra before | Ultra after |
|---|---|---|
| car sedan | 32200 | 107272 |
| car sports | 45512 | 120584 |
| car police | 39864 | 114936 |
| building cottage | 34964 | 46732 |
| building townhouse | 65352 | 90176 |
| building barn | 38924 | 52996 |
| building shed | 25296 | 32412 |
| building flat_block | 113080 | 113908 |
| human average | 114976 | 42126 |
| human hero | 120832 | 43602 |
| roof gable | 912 | 12680 |
| roof hip | 912 | 23432 |

## Triangle counts (every start design of every template, regenerated with the picture tool's counting; Windows and Linux give the same numbers)

| Template | Start design | Low | Medium | High | Ultra |
|---|---|---|---|---|---|
| fence | picket | 768 | 768 | 928 | 17552 |
| fence | post_rail | 236 | 476 | 1020 | 26112 |
| fence | ranch | 240 | 240 | 560 | 15360 |
| fence | wrought_iron | 1070 | 2046 | 4542 | 108416 |
| fence | chain_link | 4328 | 4520 | 10472 | 39312 |
| fence | stockade | 672 | 672 | 944 | 20544 |
| fence | low_decorative | 1328 | 2452 | 5276 | 116592 |
| wall | plaster | 340 | 340 | 1164 | 6100 |
| wall | brick_door | 536 | 536 | 1732 | 17048 |
| wall | garden | 24 | 24 | 96 | 5568 |
| wall | barn | 800 | 800 | 2404 | 13520 |
| wall | blank | 12 | 12 | 236 | 3504 |
| stairs | straight | 384 | 384 | 864 | 23424 |
| stairs | open_slab | 816 | 816 | 1872 | 33792 |
| stairs | l_shaped | 540 | 540 | 1260 | 34560 |
| stairs | u_shaped | 732 | 732 | 1708 | 46848 |
| stairs | spiral | 712 | 864 | 1792 | 37440 |
| door | flat | 96 | 96 | 224 | 6144 |
| door | panelled | 256 | 336 | 1008 | 5496 |
| door | glazed | 120 | 120 | 280 | 7680 |
| door | barn | 204 | 204 | 476 | 7296 |
| door | double | 420 | 420 | 980 | 11328 |
| window | plain | 72 | 72 | 168 | 4608 |
| window | casement | 96 | 96 | 224 | 6144 |
| window | cross | 132 | 132 | 308 | 8448 |
| window | colonial | 240 | 240 | 560 | 10176 |
| window | tall_ornate | 756 | 756 | 1764 | 17280 |
| roof | gable | 20 | 20 | 1444 | 12680 |
| roof | hip | 20 | 20 | 2684 | 23432 |
| roof | shed | 24 | 24 | 1248 | 4776 |
| roof | flat | 60 | 60 | 140 | 3132 |
| building | cottage | 1024 | 1024 | 5248 | 46732 |
| building | townhouse | 3032 | 3032 | 14720 | 90176 |
| building | barn | 1816 | 1816 | 8252 | 52996 |
| building | shed | 432 | 432 | 2868 | 32412 |
| building | flat_block | 5128 | 5128 | 20136 | 113908 |
| car | sedan | 528 | 916 | 8928 | 107272 |
| car | hatchback | 612 | 1120 | 9612 | 111736 |
| car | coupe | 788 | 1160 | 9508 | 109240 |
| car | suv | 584 | 1012 | 9136 | 104184 |
| car | pickup | 552 | 884 | 8880 | 106504 |
| car | sports | 1008 | 1460 | 10432 | 120584 |
| car | van | 584 | 1012 | 9136 | 104184 |
| car | taxi | 656 | 1140 | 9408 | 111096 |
| car | police | 688 | 1212 | 9560 | 114936 |
| truck | box_truck | 656 | 1180 | 7836 | 37224 |
| truck | semi_cab | 652 | 1148 | 7796 | 49296 |
| truck | tanker | 892 | 1680 | 10680 | 55328 |
| truck | light_truck | 416 | 656 | 4960 | 26960 |
| truck | tipper | 804 | 1424 | 8384 | 51576 |
| truck | long_haul | 1500 | 2116 | 11780 | 58816 |
| bus | city | 584 | 920 | 5528 | 33136 |
| bus | coach | 644 | 1124 | 7980 | 49456 |
| bus | minibus | 464 | 760 | 5176 | 32336 |
| bus | school | 636 | 1108 | 6164 | 44528 |
| bus | shuttle | 500 | 836 | 5332 | 36224 |
| bus | long_city | 808 | 1352 | 8224 | 47640 |
| motorcycle | standard | 672 | 1636 | 7164 | 50976 |
| motorcycle | cruiser | 868 | 2052 | 8668 | 55832 |
| motorcycle | sport | 920 | 1888 | 7488 | 47136 |
| motorcycle | scooter | 580 | 1408 | 6272 | 39452 |
| motorcycle | dirt | 672 | 1456 | 6880 | 41952 |
| motorcycle | touring | 764 | 1880 | 8168 | 57280 |
| trailer | box | 228 | 396 | 2612 | 12392 |
| trailer | open | 340 | 596 | 4836 | 23648 |
| trailer | flatbed | 372 | 708 | 5044 | 19984 |
| trailer | tanker | 540 | 1084 | 7604 | 30648 |
| trailer | caravan | 232 | 368 | 2552 | 15184 |
| trailer | utility | 196 | 284 | 1964 | 14584 |
| human | average | 3156 | 8656 | 21356 | 42126 |
| human | slim | 3340 | 9286 | 22940 | 45152 |
| human | athletic | 2832 | 7656 | 19044 | 37758 |
| human | heavy | 3056 | 8146 | 20084 | 39502 |
| human | child | 2788 | 7602 | 18866 | 38214 |
| human | elder | 3808 | 9860 | 24824 | 50954 |
| human | tall_slim | 3590 | 9738 | 23878 | 46578 |
| human | woman | 2864 | 7712 | 19142 | 38022 |
| human | man | 3388 | 9344 | 22932 | 44946 |
| human | adventurer | 4160 | 10598 | 26378 | 55828 |
| human | hero | 3264 | 8008 | 19802 | 43602 |
| human | casual | 4222 | 10810 | 26954 | 54794 |
| human | dress | 3390 | 8444 | 21126 | 43998 |
| robot | standard | 312 | 312 | 680 | 18432 |
| robot | heavy_bot | 356 | 564 | 1476 | 34752 |
| robot | scout | 412 | 784 | 1916 | 39856 |
| robot | cyclops | 284 | 556 | 1444 | 34048 |
| robot | android | 280 | 536 | 1372 | 25840 |
| robot | rover | 384 | 536 | 1384 | 32448 |
| creature | dog | 332 | 752 | 2064 | 27548 |
| creature | horse | 272 | 660 | 2004 | 25564 |
| creature | bear | 276 | 716 | 2228 | 33948 |
| creature | stag | 368 | 848 | 2248 | 37788 |
| creature | eagle | 222 | 482 | 1442 | 17894 |
| creature | duck | 226 | 506 | 1490 | 19046 |
| creature | dragon | 380 | 856 | 2264 | 33212 |
| creature | wyvern | 368 | 836 | 2220 | 31676 |
| creature | beetle | 312 | 792 | 2356 | 31644 |
| creature | ant | 292 | 812 | 2532 | 31148 |
| creature | spider | 316 | 876 | 2536 | 36240 |
| tree | oak | 222 | 760 | 2766 | 18928 |
| tree | pine | 90 | 210 | 450 | 3200 |
| tree | spruce | 102 | 238 | 510 | 3712 |
| tree | birch | 272 | 1120 | 4436 | 23088 |
| tree | palm | 180 | 344 | 1028 | 4560 |
| tree | willow | 314 | 756 | 2550 | 13848 |
| tree | dead | 178 | 396 | 862 | 8192 |
| tree | apple | 304 | 1224 | 5028 | 25808 |
| tree | blossom | 376 | 1600 | 6732 | 29328 |
| rock | boulder | 20 | 100 | 440 | 1840 |
| rock | pile | 120 | 600 | 2640 | 11040 |
| rock | flat_stones | 60 | 60 | 140 | 3840 |
| rock | spikes | 30 | 40 | 90 | 3200 |
| rock | basalt | 168 | 420 | 924 | 6048 |
| rock | standing | 12 | 12 | 28 | 768 |
| rock | pebbles | 280 | 1400 | 6160 | 25760 |
| rock | mossy | 112 | 592 | 2656 | 23232 |
| rock | cliff_stack | 72 | 72 | 168 | 4608 |
| road | street | 108 | 108 | 252 | 6912 |
| road | country | 36 | 36 | 84 | 2304 |
| road | dirt_path | 12 | 12 | 28 | 768 |
| road | cobble | 2916 | 2916 | 6804 | 15996 |
| road | highway | 60 | 60 | 108 | 2688 |
| road | footpath | 252 | 252 | 588 | 4032 |
| road | railway | 432 | 432 | 1008 | 2160 |
| road | gravel_drive | 1236 | 3036 | 15084 | 67596 |
| bridge | footbridge | 684 | 684 | 1596 | 20160 |
| bridge | stone_arch | 72 | 92 | 180 | 4464 |
| bridge | rope_bridge | 968 | 1096 | 2536 | 13248 |
| bridge | suspension | 892 | 1132 | 2588 | 11224 |
| bridge | truss | 1176 | 1176 | 2744 | 5880 |
| bridge | log | 312 | 632 | 1368 | 7088 |
| hedge | box | 12 | 12 | 28 | 768 |
| hedge | rounded | 20 | 28 | 100 | 4416 |
| hedge | tall_screen | 12 | 12 | 28 | 768 |
| hedge | low_border | 20 | 28 | 100 | 4416 |
| hedge | domed | 20 | 28 | 100 | 4416 |
| hedge | topiary_balls | 60 | 204 | 892 | 4416 |
| hedge | topiary_cones | 38 | 70 | 190 | 7488 |
| plant | bush | 32 | 192 | 896 | 3840 |
| plant | flowers | 612 | 1512 | 6516 | 27216 |
| plant | sunflowers | 540 | 1340 | 6120 | 26120 |
| plant | ferns | 144 | 216 | 1080 | 18432 |
| plant | cactus | 44 | 108 | 236 | 6144 |
| plant | mushrooms | 110 | 440 | 1700 | 6620 |
| plant | reeds | 432 | 432 | 1008 | 6912 |
| plant | grass | 192 | 192 | 1056 | 18432 |
| plant | potted | 52 | 236 | 988 | 6144 |
| plant | planter | 320 | 720 | 3008 | 15168 |
| streetlamp | victorian | 118 | 230 | 486 | 6656 |
| streetlamp | modern | 64 | 120 | 264 | 6912 |
| streetlamp | bollard | 48 | 148 | 544 | 4528 |
| streetlamp | double_arm | 116 | 324 | 1076 | 10492 |
| streetlamp | lantern_post | 54 | 54 | 118 | 3200 |
| streetlamp | rural | 72 | 186 | 590 | 6590 |
| furniture | chair | 144 | 144 | 336 | 9216 |
| furniture | table | 380 | 780 | 1660 | 13440 |
| furniture | round_table | 132 | 260 | 548 | 13824 |
| furniture | armchair | 168 | 240 | 640 | 6336 |
| furniture | sofa | 136 | 288 | 848 | 12576 |
| furniture | bed | 276 | 276 | 644 | 7872 |
| furniture | bookshelf | 108 | 108 | 252 | 6912 |
| furniture | desk | 160 | 160 | 336 | 8832 |
| furniture | wardrobe | 108 | 108 | 252 | 6912 |
| container | crate | 48 | 48 | 112 | 3072 |
| container | barrel | 168 | 364 | 756 | 10752 |
| container | chest | 76 | 76 | 156 | 4080 |
| container | bucket | 36 | 72 | 144 | 3648 |
| container | jar | 132 | 332 | 876 | 4800 |
| container | sack | 48 | 144 | 584 | 3744 |
| tool | hammer | 48 | 108 | 356 | 7424 |
| tool | axe | 96 | 192 | 400 | 3264 |
| tool | pickaxe | 40 | 40 | 72 | 1824 |
| tool | shovel | 44 | 76 | 140 | 3360 |
| tool | wrench | 116 | 252 | 668 | 8576 |
| tool | sword | 108 | 244 | 628 | 7280 |
| decoration | signpost | 48 | 48 | 112 | 3072 |
| decoration | arrow_sign | 36 | 36 | 52 | 1104 |
| decoration | flagpole | 60 | 148 | 420 | 8624 |
| decoration | statue | 136 | 340 | 1100 | 6592 |
| decoration | garden_lantern | 42 | 42 | 90 | 2432 |
| decoration | fountain | 184 | 396 | 820 | 7392 |

## The human at every Detail level (rebuilt)

The human no longer switches to a different, blocky generator at Low (only the style "Blocky" does). Every level is the same
person built from rings and curves; Detail changes how many rings and sides each tube has and how many small parts there are
(lids, rims, finger joints, buttons). It is not subdivided at Ultra any more (the old Catmull-Clark pass shrank open joints and
cost 120 thousand triangles). Average adult, triangles (`tests/test_guided_human.cpp`): Low about 3 thousand, Medium 8.6 thousand,
High 21.5 thousand, Ultra 44 thousand. Pictures: `docs/images/guided_modeling/human_views_average.png` (front, side and 3/4 at the four
levels), `human_close_average.png` (head, hand, foot, shoulder at High), `human_all_detail0/1/2.png` (every start design),
`human_views_average_before.png` (the old human, for comparison). All are headless software renders.

### The newer templates (aircraft, boat, weapon, tower, lamp, tent), triangles

| Template | Start design | Low | Medium | High | Ultra |
|---|---|---|---|---|---|
| aircraft | light_plane | 282 | 608 | 1872 | 9016 |
| aircraft | biplane | 306 | 620 | 1700 | 12296 |
| aircraft | jet_fighter | 236 | 512 | 1492 | 7768 |
| aircraft | airliner | 404 | 748 | 1768 | 7432 |
| aircraft | cargo_plane | 300 | 580 | 1440 | 8936 |
| aircraft | glider | 224 | 500 | 1480 | 7576 |
| boat | rowboat | 130 | 202 | 418 | 4992 |
| boat | canoe | 118 | 190 | 390 | 4224 |
| boat | speedboat | 126 | 210 | 418 | 4800 |
| boat | sailboat | 130 | 222 | 486 | 9312 |
| boat | tall_ship | 162 | 274 | 666 | 7776 |
| boat | steamer | 134 | 278 | 766 | 8832 |
| boat | raft | 64 | 184 | 760 | 8972 |
| weapon | longsword | 106 | 294 | 1222 | 8488 |
| weapon | saber | 90 | 258 | 1026 | 7660 |
| weapon | dagger | 106 | 294 | 1222 | 8488 |
| weapon | spear | 14 | 38 | 170 | 2304 |
| weapon | bow | 56 | 212 | 908 | 3740 |
| weapon | mace | 92 | 248 | 976 | 12444 |
| weapon | pistol | 44 | 72 | 240 | 3648 |
| weapon | rifle | 56 | 84 | 268 | 3264 |
| weapon | kite_shield | 38 | 88 | 324 | 1984 |
| weapon | round_shield | 50 | 100 | 336 | 1996 |
| tower | castle_tower | 252 | 456 | 1392 | 15872 |
| tower | round_keep | 164 | 398 | 1326 | 13562 |
| tower | lighthouse | 164 | 524 | 2436 | 21176 |
| tower | bell_tower | 110 | 134 | 374 | 8064 |
| tower | water_tower | 168 | 258 | 810 | 4218 |
| tower | watch_tower | 162 | 162 | 370 | 3200 |
| tower | radio_mast | 60 | 152 | 496 | 2648 |
| tower | octagon_tower | 136 | 176 | 432 | 9152 |
| lamp | table_lamp | 86 | 324 | 1356 | 13388 |
| lamp | floor_lamp | 40 | 204 | 884 | 12032 |
| lamp | desk_lamp | 80 | 208 | 656 | 11520 |
| lamp | globe_lamp | 68 | 284 | 1128 | 11920 |
| lamp | hand_lantern | 46 | 162 | 666 | 9856 |
| lamp | chandelier | 188 | 488 | 1736 | 6392 |
| tent | a_frame | 40 | 52 | 108 | 2832 |
| tent | dome_tent | 114 | 362 | 1390 | 10174 |
| tent | tipi | 154 | 386 | 1446 | 12820 |
| tent | yurt | 52 | 194 | 786 | 6138 |
| tent | market_stall | 44 | 92 | 188 | 4800 |
| tent | camp | 208 | 624 | 2504 | 14720 |
