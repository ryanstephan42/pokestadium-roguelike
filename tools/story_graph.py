"""Host-only candidate traversal policy. Never marks the canonical catalog reviewed."""

from tools.story_data import require


GYMS = {
    "PewterGym": "BROCK", "CeruleanGym": "MISTY", "VermilionGym": "LT_SURGE",
    "CeladonGym": "ERIKA", "FuchsiaGym": "KOGA", "SaffronGym": "SABRINA",
    "CinnabarGym": "BLAINE", "ViridianGym": "GIOVANNI",
}


def candidate(data, version, rival_branch=0, fossil_choice=0):
    """Explicit region abstraction for review; clauses are alternative all-of sets."""
    require(version in ("red", "blue"), "Unknown story version")
    require(rival_branch in (0, 1, 2) and fossil_choice in (0, 1),
            "Invalid locked graph choice")
    entries = data["encounters"][version]
    by_id = {e["id"]: e for e in entries}
    bosses = {}
    for area, cls in GYMS.items():
        matches = [e["id"] for e in entries if e["area"] == area and e.get("class") == cls]
        require(len(matches) == 1, f"Missing graph boss: {area}")
        bosses[area] = matches[0]
    areas = {}

    def region(names, *clauses):
        for area in names.split():
            require(area not in areas, f"Duplicate graph area: {area}")
            areas[area] = [list(clause) for clause in clauses]

    region("OaksLab", ())
    region("ViridianForest PewterGym", ("OaksLab/rival-lab",))
    region("Route3 Route4 MtMoon1F MtMoonB2F MtMoonPokecenter", (bosses["PewterGym"],))
    region("CeruleanCity CeruleanGym", ("travel:moon-exit",))
    region("Route24 Route25", ("CeruleanCity/rival-bridge",))
    region("Route6 Route11", ("travel:bill",))
    region("SSAnne1FRooms SSAnne2F SSAnne2FRooms SSAnneB1FRooms SSAnneBow",
           ("travel:bill",))
    region("VermilionGym", ("field:cut",))
    region("Route9 Route10 RockTunnel1F RockTunnelB1F Route8 PokemonTower2F "
           "PokemonTower3F PokemonTower4F PokemonTower5F PokemonTower6F "
           "CeladonGym CeladonMansionRoofHouse GameCorner", ("field:cut",))
    region("RocketHideoutB1F RocketHideoutB2F RocketHideoutB3F RocketHideoutB4F",
           ("GameCorner/TEXT_GAMECORNER_ROCKET",))
    region("PokemonTower7F", ("PokemonTower6F/ghost",))
    region("FightingDojo", ("travel:saffron",))
    region("SaffronGym", ("travel:saffron", "SilphCo11F/TEXT_SILPHCO11F_GIOVANNI"))
    region("SilphCo2F SilphCo3F SilphCo4F SilphCo5F SilphCo6F SilphCo7F "
           "SilphCo8F SilphCo9F SilphCo10F SilphCo11F", ("travel:silph",))
    region("Route12", ("field:cut",))
    region("Route16", ("field:cut",))
    region("Route13 Route14 Route15", ("Route12/snorlax",), ("travel:fuchsia",))
    region("Route17 Route18", ("Route16/snorlax", "travel:bicycle"), ("travel:fuchsia",))
    region("FuchsiaGym", ("travel:fuchsia",))
    region("Route19 Route20 Route21 PokemonMansion1F PokemonMansion2F PokemonMansion3F "
           "PokemonMansionB1F CinnabarLabFossilRoom", ("field:surf",))
    region("SeafoamIslandsB4F", ("field:surf", "field:strength"))
    region("PowerPlant", ("field:surf",))
    region("CinnabarGym", ("field:surf", "travel:secret-key"))
    first_seven = tuple(bosses[area] for area in GYMS if area != "ViridianGym")
    region("ViridianGym", first_seven)
    region("Route22", ("OaksLab/rival-lab",))
    region("VictoryRoad1F VictoryRoad2F VictoryRoad3F",
           tuple(bosses.values()) + ("field:surf", "field:strength", "Route22/rival-late"))
    region("LoreleisRoom", ("travel:league",))
    for area, previous in (("BrunosRoom", "LoreleisRoom"), ("AgathasRoom", "BrunosRoom"),
                           ("LancesRoom", "AgathasRoom"), ("ChampionsRoom", "LancesRoom")):
        matches = [e["id"] for e in entries if e["area"] == previous]
        require(len(matches) == 1, f"Ambiguous League predecessor: {previous}")
        region(area, (matches[0],))
    region("CeruleanCaveB1F", ("ChampionsRoom/rival-champion", "field:surf"))
    require(set(areas) == {e["area"] for e in entries},
            "Graph area coverage differs from inventory")
    nodes = {e["id"]: {"clauses": [list(c) for c in areas[e["area"]]],
                      "kind": e["kind"], "variants": len(e["teams"]),
                      "variant": rival_branch if e["kind"] == "rival" else 0}
             for e in entries}

    def extra(identifier, *dependencies):
        require(identifier in nodes, f"Unknown graph encounter: {identifier}")
        nodes[identifier]["clauses"] = [clause + list(dependencies)
                                         for clause in nodes[identifier]["clauses"]]

    def action(identifier, *clauses, variants=1):
        require(identifier not in nodes, f"Duplicate graph action: {identifier}")
        nodes[identifier] = {"clauses": [list(c) for c in clauses],
                            "kind": "action", "variants": variants}

    fossil_guard = "MtMoonB2F/TEXT_MTMOONB2F_SUPER_NERD"
    action("travel:fossil", (fossil_guard,), variants=2)
    action("travel:moon-exit", ("travel:fossil",))
    bridge = tuple("Route24/TEXT_ROUTE24_" + suffix for suffix in
                   ("YOUNGSTER2", "COOLTRAINER_F2", "YOUNGSTER1",
                    "COOLTRAINER_F1", "COOLTRAINER_M3"))
    extra("Route24/TEXT_ROUTE24_COOLTRAINER_M1", *bridge)
    action("travel:bill", ("Route24/TEXT_ROUTE24_COOLTRAINER_M1",))
    action("event:hm01-received", ("SSAnne2F/rival-ship",))
    nodes["event:hm01-received"]["source_action"] = "SSAnneCaptainsRoom/HM01"
    nodes["event:hm01-received"]["sets_event"] = "EVENT_GOT_HM01"
    action("travel:ship-departure", ("event:hm01-received",))
    nodes["travel:ship-departure"]["destination_warp"] = 1
    action("field:cut", ("event:hm01-received", bosses["CeruleanGym"]))
    action("travel:amber", ("field:cut",))
    action("travel:saffron", ("field:cut",))
    hideout = "RocketHideoutB4F/TEXT_ROCKETHIDEOUTB4F_"
    extra(hideout + "GIOVANNI", hideout + "ROCKET1", hideout + "ROCKET2", hideout + "ROCKET3")
    action("travel:scope", (hideout + "GIOVANNI",))
    extra("PokemonTower6F/ghost", "travel:scope")
    tower_rockets = tuple("PokemonTower7F/TEXT_POKEMONTOWER7F_ROCKET" + str(i)
                          for i in (1, 2, 3))
    action("travel:fuji", tower_rockets)
    action("travel:flute", ("travel:fuji",))
    action("travel:silph", ("travel:fuji", "travel:saffron"))
    for identifier in ("Route12/snorlax", "Route16/snorlax"):
        extra(identifier, "travel:flute")
    for suffix in ("COOLTRAINER_M", "SUPER_NERD", "FISHER5"):
        identifier = "Route12/TEXT_ROUTE12_" + suffix
        nodes[identifier]["clauses"] = [["field:cut", "Route12/snorlax"],
                                         ["travel:fuchsia"]]
    for index in range(1, 7):
        nodes[f"Route16/TEXT_ROUTE16_BIKER{index}"]["clauses"] = [
            ["Route16/snorlax", "travel:bicycle"], ["travel:fuchsia", "travel:bicycle"]]
    action("travel:bicycle", ("travel:bill",))
    action("travel:fuchsia", ("Route12/snorlax",),
           ("Route16/snorlax", "travel:bicycle"))
    action("travel:surf", ("travel:fuchsia",))
    action("travel:strength", ("travel:fuchsia",))
    for index in (1, 2):
        nodes[f"Route19/TEXT_ROUTE19_COOLTRAINER_M{index}"]["clauses"] = [["travel:fuchsia"]]
    action("field:surf", ("travel:surf", bosses["FuchsiaGym"]))
    action("field:strength", ("travel:strength", bosses["CeladonGym"]))
    action("travel:secret-key", ("field:surf",))
    action("travel:card-key", ("travel:silph",))
    extra("SilphCo7F/rival-silph", "travel:card-key")
    extra("SilphCo7F/gift-lapras", "SilphCo7F/rival-silph")
    extra("SilphCo11F/TEXT_SILPHCO11F_GIOVANNI", "travel:card-key", "SilphCo7F/rival-silph",
          "SilphCo11F/TEXT_SILPHCO11F_ROCKET1")
    extra("FightingDojo/gift-dojo", "FightingDojo/TEXT_FIGHTINGDOJO_KARATE_MASTER")
    extra("CinnabarLabFossilRoom/gift-fossil", "travel:fossil")
    nodes["travel:fossil"]["variant"] = fossil_choice
    nodes["CinnabarLabFossilRoom/gift-fossil"]["variant"] = fossil_choice
    extra("CinnabarLabFossilRoom/gift-amber", "travel:amber")
    nodes["Route22/rival-late"]["clauses"] = [list(bosses.values())]
    action("travel:league", tuple(bosses.values()) +
           ("field:surf", "field:strength", "Route22/rival-late"))
    retires = {}
    for rule in data["retirement_audit"][version]:
        retires.setdefault(rule["after"], set()).update(rule["retires"])
    for rule in data["event_retirement_audit"][version]:
        require(rule["after_event"] == "EVENT_SS_ANNE_LEFT", "Unknown event retirement")
        retires["travel:ship-departure"] = set(rule["retires"])
    for node in nodes.values():
        require(node["clauses"] and all(all(dependency in nodes for dependency in clause)
                                       for clause in node["clauses"]), "Dangling graph dependency")
    for trigger, identifiers in retires.items():
        require(trigger in nodes and identifiers <= by_id.keys(),
                "Dangling graph retirement")
    return {"nodes": nodes, "retires": retires, "encounters": by_id,
            "reviewed": False, "fossil_offer": "CinnabarLabFossilRoom/gift-fossil",
            "healing": healing_permissions(data, version, bosses)}


def healing_permissions(data, version, bosses):
    gates = {
        "ViridianPokecenter": (), "RedsHouse1F": (),
        "PewterPokecenter": ("OaksLab/rival-lab",),
        "MtMoonPokecenter": (bosses["PewterGym"],),
        "CeruleanPokecenter": ("travel:moon-exit",),
        "VermilionPokecenter": ("travel:bill",),
        "RockTunnelPokecenter": ("field:cut",),
        "LavenderPokecenter": ("field:cut",), "CeladonPokecenter": ("field:cut",),
        "SaffronPokecenter": ("travel:saffron",),
        "FuchsiaPokecenter": ("travel:fuchsia",),
        "CinnabarPokecenter": ("field:surf",),
        "PokemonTower5F": ("field:cut",),
        "SilphCo9F": ("travel:silph", "travel:card-key"),
        "IndigoPlateauLobby": ("travel:league",),
    }
    permissions = {}
    for fact in data["healing_fact_audit"][version]:
        area = fact["id"][5:]
        require(area in gates and fact["id"] not in permissions, "Unknown healing permission")
        permissions[fact["id"]] = {"required": gates[area], "retires_after": fact["retires_after"]}
    require(len(permissions) == len(gates), "Missing healing permission")
    return permissions


def healing_available(graph, completed, identifier, preset, used=()):
    require(identifier in graph["healing"] and preset in ("standard", "limited", "brutal"),
            "Invalid healing permission request")
    permission = graph["healing"][identifier]
    league_active = any(identifier.startswith(("LoreleisRoom/", "BrunosRoom/",
                                               "AgathasRoom/", "LancesRoom/"))
                        for identifier in completed) and "ChampionsRoom/rival-champion" not in completed
    return (preset != "brutal" and not league_active and
            (preset != "limited" or identifier not in used) and
            set(permission["required"]) <= completed and
            permission["retires_after"] not in completed)


def available(graph, completed, retired, identifier, destination_warp=None):
    require(identifier in graph["nodes"], "Unknown graph node")
    node = graph["nodes"][identifier]
    return (("destination_warp" not in node or
             destination_warp == node["destination_warp"]) and
            identifier not in completed and identifier not in retired and
            any(set(clause) <= completed for clause in graph["nodes"][identifier]["clauses"]))


def advance(graph, completed, retired, identifier, destination_warp=None):
    require(available(graph, completed, retired, identifier, destination_warp),
            "Unavailable graph node")
    completed.add(identifier)
    retired.update(graph["retires"].get(identifier, set()) - completed)


def traverse(graph, priorities=()):
    """Deterministic host reachability sweep; no battles, RNG, saves or native state."""
    completed, retired = set(), set()
    order = []
    ranks = {identifier: index for index, identifier in enumerate(priorities)}
    while True:
        ready = [identifier for identifier in graph["nodes"]
                 if available(graph, completed, retired, identifier, destination_warp=1)]
        if not ready:
            break
        # Delay destructive milestones until all currently available optional nodes.
        ready.sort(key=lambda identifier: (bool(graph["retires"].get(identifier)),
                                          ranks.get(identifier, len(ranks)), identifier))
        identifier = ready[0]
        advance(graph, completed, retired, identifier, destination_warp=1)
        order.append(identifier)
    require(len(order) == len(set(order)), "Repeated finite graph node")
    return order, retired
