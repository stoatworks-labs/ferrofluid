/*
 * Stoatworks Labs - About window data for Ferrofluid.
 *
 * PROVISIONAL HAND COPY, adapted from schlieren's generated file on
 * 2026-10-15, with guide = "" (no user guide exists yet), as graticule did.
 * Once the project is registered, stoatworks-backend/scripts/sync-about.py
 * generates this file from the website's projects.json; edit it there then.
 */
#pragma once

namespace stoatworks::about
{
    inline constexpr auto name = "Ferrofluid";
    inline constexpr auto slug = "ferrofluid";
    inline constexpr auto hook = "Ferrofluid spikes on the beat, for Resolume";
    inline constexpr auto licence = "MIT";
    inline constexpr auto guide = "";
    inline constexpr auto page = "https://stoatworks-labs.com/software/ferrofluid/";
    inline constexpr auto repo = "https://github.com/stoatworks-labs/ferrofluid";
    inline constexpr auto versionFallback = "v0.1.0";

    inline constexpr auto org = "Stoatworks Labs";
    inline constexpr auto home = "https://stoatworks-labs.com";
    inline constexpr auto tagline = "Open tools for the people who run the show.";

    /* The canonical funding links, matching FUNDING.yml and the support footer. */
    struct Link { const char* name; const char* url; };
    inline constexpr Link funding[] = {
        { "GitHub Sponsors", "https://github.com/sponsors/stoatworks-labs" },
        { "Ko-fi", "https://ko-fi.com/stoatworkslabs" },
        { "Patreon", "https://patreon.com/StoatworksLabs" },
        { "Liberapay", "https://liberapay.com/stoatworks-labs" },
    };
}
