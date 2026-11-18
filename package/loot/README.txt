NorseLoot rules
===============

Every *.ini here adds entries to (or removes them from) item lists inside the game's data: a trader's stock, a spawn
point's loot. Several packs can add to the same list - nothing is overwritten, and nothing in the game's files changes.

    [TraderProfile]                         ; the kind of object (its class)
    List = Tiers[*].ItemPools[*].Items      ; where its entries live: Field.Sub, List[n] or List[*] for every element
    Load = /Game/Norse/Core/Data/Worksites/TraderProfiles/TravelingMerchants/Merchant_Profile_Boat_01.Merchant_Profile_Boat_01
    Add = (Item="/Game/Norse/Core/Data/Items/Weapons/Axes/Tier1_Common_Axe_01.Tier1_Common_Axe_01",SuperiorChance=10,MaxQuantity=2) | Merchant_Profile_Boat_*
    Remove = Tier1_Common_Sword | Merchant_Profile_Boat_01

Add = an entry in Unreal's text form, then optional filters: names | folders | field filters (Field:text, Tag:Name) |
chance. A list that already holds the entry's asset is left alone. Remove = text; every entry containing it goes.
Load = an asset to load at each map load (only loaded objects are changed). Console: nl.dump <object path> (its fields
and lists - how to find the List path), nl.reload, nl.apply.
