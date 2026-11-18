# NorseLoot

Rewards, shops and stashes by rule for the Norse overhaul (NORSE: Oath of Blood, UE4SS v4.0.0). Rules add or remove
entries in item lists nested anywhere in loaded objects - a trader's stock, a spawn point's loot - so several packs
add to the same list without overwriting it. Modelled on Container Item Distributor. Survey:
`RESEARCH-more-frameworks.md` (#13).

```ini
[TraderProfile]
List = Tiers[*].ItemPools[*].Items
Load = /Game/Norse/Core/Data/Worksites/TraderProfiles/TravelingMerchants/Merchant_Profile_Boat_01.Merchant_Profile_Boat_01
Add = (Item="/Game/Norse/Core/Data/Items/Weapons/Axes/Tier1_Common_Axe_01.Tier1_Common_Axe_01",SuperiorChance=10,MaxQuantity=2) | | Merchant_Profile_Boat_01
Remove = Utility_Tier2_Net | | Merchant_Profile_Boat_01
```

Author guide: `package\loot\README.txt`; the tested example: `docs\examples\test-loot.ini`. Where Norse keeps things:
a travelling merchant (`Worksites/TraderProfiles/...`, a TravellerProfile) holds its stock in the sub-object
`:TraderProfile_0`, `Tiers[*].ItemPools[*].Items` (each entry: Item, SuperiorChance, MaxQuantity, CraftedBy); spawn
points carry `LootItems`. Every trader's stock sub-object is named TraderProfile_0, so tell traders apart by folder.

Paths go through structs and lists with the shared `ResolveArrays` (`../NorsePatcher/include/NorseReflect.hpp`): Field,
Field.Sub, List[n], List[*]. An Add is skipped where the list already holds the entry's asset; a Remove drops every
entry containing its text. Applied at each map load; console `nl.reload`, `nl.apply`, `nl.dump <object path>`;
NorseDevTools reloads the rules when a file in `loot\` is saved.

0.1.0 tested in game (TEST-RESULT-0.1.0.txt). Not yet: mission rewards (where Norse keeps them is still to find),
spawn-point loot in game.
