// Test-only GPL process boundary for the pinned PKHeX.Core reference.
//
// This source is compiled outside the shipped runtime/port. The resulting
// executable is a local verification artifact governed by PKHeX's GPL terms.
using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Text.Json;
using PKHeX.Core;

if (args.Length != 1)
{
    Console.Error.WriteLine("usage: pkhex-save-oracle <crystal.sav>");
    return 2;
}

var path = args[0];
var data = File.ReadAllBytes(path);
if (data.Length != 0x8000)
{
    Console.Error.WriteLine("expected a 32768-byte international Gen II save");
    return 2;
}

var save = new SAV2(data, LanguageID.English, GameVersion.C);

static object DecodePokemon(PKM pokemon) => new
{
    species = pokemon.Species,
    nickname = pokemon.Nickname,
    original_trainer = pokemon.OriginalTrainerName,
    level = pokemon.CurrentLevel,
    held_item = pokemon.HeldItem,
    moves = new[]
    {
        pokemon.Move1,
        pokemon.Move2,
        pokemon.Move3,
        pokemon.Move4,
    },
    status = pokemon.Status_Condition,
    hp = pokemon.Stat_HPCurrent,
    max_hp = pokemon.Stat_HPMax,
};

var party = new List<object>();
for (var slot = 0; slot < save.PartyCount; ++slot)
    party.Add(DecodePokemon(save.GetPartySlotAtIndex(slot)));

var boxes = new List<object>();
for (var box = 0; box < save.BoxCount; ++box)
{
    var pokemon = save.GetBoxData(box)
        .Where(entity => entity.Species != 0)
        .Select(DecodePokemon)
        .ToArray();
    boxes.Add(new
    {
        index = box,
        name = save.GetBoxName(box),
        pokemon,
    });
}

var caught = new List<int>();
var seen = new List<int>();
for (ushort species = 1; species <= 251; ++species)
{
    if (save.GetCaught(species))
        caught.Add(species);
    if (save.GetSeen(species))
        seen.Add(species);
}

var result = new
{
    schema = "crystal-recompiled.pkhex-save-oracle",
    version = 1,
    accepted = save.ChecksumsValid,
    checksum_info = save.ChecksumInfo,
    player = new
    {
        name = save.OT,
        trainer_id = save.TID16,
        gender = save.Gender,
        money = save.Money,
        badges = save.Badges,
        played_hours = save.PlayedHours,
        played_minutes = save.PlayedMinutes,
        played_seconds = save.PlayedSeconds,
    },
    pokedex = new
    {
        caught,
        seen,
    },
    party,
    current_box = save.CurrentBox,
    boxes,
};

Console.WriteLine(JsonSerializer.Serialize(
    result,
    new JsonSerializerOptions { WriteIndented = true }));
return save.ChecksumsValid ? 0 : 3;
