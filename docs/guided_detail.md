# Guided modeling: detail levels

Every guided template (fence, architecture, vehicles) has a shared **Detail** setting: Low, Medium, High, Ultra. It is
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

## Triangle counts (from the test, per start design)

| Template | Start design | Low | Medium | High | Ultra |
|---|---|---|---|---|---|
| fence | picket | 768 | 768 | 928 | 1248 |
| fence | post_rail | 236 | 476 | 1020 | 2108 |
| fence | ranch | 240 | 240 | 560 | 1200 |
| fence | wrought_iron | 1070 | 2046 | 4542 | 11070 |
| fence | chain_link | 4328 | 4520 | 10472 | 22376 |
| fence | stockade | 672 | 672 | 944 | 1488 |
| fence | low_decorative | 1328 | 2452 | 5276 | 13228 |
| wall | plaster | 340 | 340 | 688 | 1420 |
| wall | brick_door | 536 | 536 | 1060 | 2144 |
| wall | garden | 24 | 24 | 68 | 192 |
| wall | barn | 800 | 800 | 1676 | 3464 |
| wall | blank | 12 | 12 | 40 | 132 |
| stairs | straight | 384 | 384 | 864 | 1824 |
| stairs | open_slab | 816 | 816 | 1872 | 3984 |
| stairs | l_shaped | 540 | 540 | 1260 | 2700 |
| stairs | u_shaped | 732 | 732 | 1708 | 3660 |
| stairs | spiral | 712 | 864 | 1792 | 3648 |
| door | flat | 96 | 96 | 224 | 480 |
| door | panelled | 256 | 336 | 1008 | 3120 |
| door | glazed | 120 | 120 | 280 | 600 |
| door | barn | 204 | 204 | 476 | 1020 |
| door | double | 420 | 420 | 980 | 2100 |
| window | plain | 72 | 72 | 168 | 360 |
| window | casement | 96 | 96 | 224 | 480 |
| window | cross | 132 | 132 | 308 | 660 |
| window | colonial | 240 | 240 | 560 | 1200 |
| window | tall_ornate | 756 | 756 | 1764 | 3780 |
| roof | gable | 20 | 20 | 36 | 68 |
| roof | hip | 20 | 20 | 36 | 68 |
| roof | shed | 24 | 24 | 40 | 72 |
| roof | flat | 60 | 60 | 140 | 300 |
| building | cottage | 1024 | 1024 | 2128 | 4480 |
| building | townhouse | 3032 | 3032 | 5880 | 11720 |
| building | barn | 1816 | 1816 | 3928 | 8296 |
| building | shed | 432 | 432 | 928 | 2064 |
| building | flat_block | 5128 | 5128 | 9912 | 19624 |
| car | sedan | 440 | 736 | 2384 | 5984 |
| car | hatchback | 524 | 940 | 3068 | 8396 |
| car | coupe | 700 | 980 | 2964 | 7236 |
| car | suv | 496 | 832 | 2592 | 6416 |
| car | pickup | 464 | 704 | 2336 | 5904 |
| car | sports | 920 | 1280 | 3888 | 10176 |
| car | van | 496 | 832 | 2592 | 6416 |
| car | taxi | 568 | 960 | 2864 | 6976 |
| car | police | 600 | 1032 | 3016 | 7288 |
| truck | box_truck | 656 | 1180 | 3804 | 9508 |
| truck | semi_cab | 652 | 1148 | 3764 | 9452 |
| truck | tanker | 892 | 1680 | 5304 | 13160 |
| truck | light_truck | 416 | 656 | 2272 | 5808 |
| truck | tipper | 804 | 1424 | 4352 | 10664 |
| truck | long_haul | 1500 | 2116 | 6404 | 15588 |
| bus | city | 584 | 920 | 2840 | 6984 |
| bus | coach | 644 | 1124 | 3948 | 10820 |
| bus | minibus | 464 | 760 | 2488 | 6248 |
| bus | school | 636 | 1108 | 3476 | 9284 |
| bus | shuttle | 500 | 836 | 2644 | 6564 |
| bus | long_city | 808 | 1352 | 4192 | 10328 |
| motorcycle | standard | 276 | 460 | 1380 | 3372 |
| motorcycle | cruiser | 480 | 744 | 2208 | 5888 |
| motorcycle | sport | 316 | 492 | 1460 | 3548 |
| motorcycle | scooter | 296 | 540 | 1740 | 4892 |
| motorcycle | dirt | 432 | 588 | 1692 | 4052 |
| motorcycle | touring | 416 | 748 | 2188 | 5820 |
| trailer | box | 228 | 396 | 1268 | 3164 |
| trailer | open | 340 | 596 | 2148 | 5556 |
| trailer | flatbed | 372 | 708 | 2356 | 5956 |
| trailer | tanker | 540 | 1084 | 3572 | 9004 |
| trailer | caravan | 232 | 368 | 1208 | 3040 |
| trailer | utility | 196 | 284 | 620 | 1292 |
