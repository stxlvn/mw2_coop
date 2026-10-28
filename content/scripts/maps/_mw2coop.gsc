// mw2coop: campaign co-op framework for Call of Duty: Modern Warfare 2 (2009).
// Our own code, loaded through mw2coop's script loader.
//
// Co-op runs in LOCKSTEP: host and client both run every level script. Everything here must
// give the same result on both machines from the same game state. Never branch on anything
// local (local-only dvars, files, wall-clock time) when that changes game state.
//
// Hooks into the game (see mw2coop/patches):
//   maps\_load::main()                     -> main()
//   maps\_utility::is_specialop()          -> false in a co-op campaign session
//   maps\_utility::missionFailedWrapper()  -> mission_failed()
//   maps\_utility::nextmission()           -> mission_complete()
// Requests to the host's mw2coop DLL go through the dvar "mw2coop_cmd" (see request()).

#include common_scripts\utility;
#include maps\_utility;
#include maps\_hud_util;

// A co-op session on a campaign map: the campaign, played by two.
is_campaign_coop()
{
	if ( GetDvar( "coop" ) != "1" )
		return false;
	return !string_starts_with( ToLower( GetDvar( "mapname" ) ), "so_" );
}

// Level scripts call these before maps\_load::main().
//   flag_name: progress flag of the level; start_name: the level's start point that resumes there.
add_checkpoint( flag_name, start_name )
{
	if ( !isdefined( level.mw2coop_checkpoints ) )
		level.mw2coop_checkpoints = [];
	cp = SpawnStruct();
	cp.flag = flag_name;
	cp.start = start_name;
	level.mw2coop_checkpoints[ level.mw2coop_checkpoints.size ] = cp;
}

split_view_gate()
{
	// The picture is split at once, while the mission's title is still up: its black screen covers both halves and its lines are drawn once over the whole picture as in single
	// player (maps\_introscreen patch: the lines' sort marks them for the native split screen), so the change cannot be seen. A slam zoom from the sky is player 1's view alone:
	// there one picture is kept until it has landed (the intro's flag, at most 45 s).
	wait 0.25;
	if ( flying_intro_level() )
	{
		if ( flag_exist( "introscreen_complete" ) && !flag( "introscreen_complete" ) )
			level waittill_notify_or_timeout( "introscreen_complete", 45 );
		for ( t = 0; t < 15 && isdefined( level.mw2coop_intro_black ) && level.mw2coop_intro_black; t += 0.1 )
			wait 0.1;
		wait 0.3;
	}
	SetDvar( "mw2coop_split_view", "1" );
}

// the levels of maps\_introscreen::flying_intro()
flying_intro_level()
{
	switch ( level.script )
	{
		case "killhouse":
		case "cliffhanger":
		case "estate":
		case "boneyard":
			return true;
		case "roadkill":
			return !getdvarint( "newintro" );
	}
	return false;
}

// maps\_introscreen patch: the mission title's black screen is up (true) or gone (false)
intro_black( on )
{
	level.mw2coop_intro_black = on;
}

intro_black_off_after( time )
{
	wait time;
	level.mw2coop_intro_black = false;
}

// Called from maps\_load::main() right after maps\_coop::main().
main()
{
	// The player a one-player scene plays on: scenes the mod hands to either player (the knife
	// fight) read level.mw2coop_lead where they read level.player. Single player too.
	level.mw2coop_lead = level.player;

	// a scene hid the partner's name (maps\_mw2coop_knife::lead_decided()): show it again
	SetDvar( "mw2coop_hide_names", "" );

	// native split screen (src/native.c): both views are shown only once the mission's intro is over; before that (briefing, title card) there is one picture
	SetDvar( "mw2coop_split_view", "0" );
	thread split_view_gate();

	if ( !is_campaign_coop() )
		return;

	level.mw2coop = SpawnStruct();
	level.mw2coop.failing = false;
	level.mw2coop.completing = false;
	level.mw2coop.checkpoint = -1;
	level.mw2coop.section = -1;	// index in level.start_functions of the furthest section reached
	if ( !isdefined( level.mw2coop_weapon_sync ) )
		level.mw2coop_weapon_sync = "leader";	// "leader": player 1's changes go to player 2; "both": gains go both ways; "own": each player takes his own (only the start's weapons are copied)
	if ( !isdefined( level.mw2coop_own_weapons ) )
		level.mw2coop_own_weapons = [];	// see own_weapon_busy()

	PrecacheString( &"SCRIPT_CHECKPOINT" );
	PrecacheString( &"MW2COOP_STUNNED" );	// the mod's own, from its DLL (src/localize.c)
	PrecacheString( &"MW2COOP_SWAP_SEATS" );

	// maps\_coop (revive) and kill_wrapper() read this Spec Ops flag in any co-op session;
	// maps\_specialops creates it, the campaign does not. Set when the level ends, as in Spec Ops,
	// so the revive HUD and downed-player threads stop.
	if ( !flag_exist( "special_op_terminated" ) )
		flag_init( "special_op_terminated" );

	state( "level " + level.script + ", start dvar '" + GetDvar( "start" ) + "', qa '" + GetDvar( "mw2coop_qa" ) + "'" );

	thread no_saves();
	fov_pilot( undefined );	// a level that ended in a pilot's view must not leave it on
	friendly_fire_setting();
	flag_init( "mw2coop_regrouped" );
	thread regroup_after_start();
	thread follow_leader_teleports();
	set_bodies();
	thread sync_weapons();
	thread track_checkpoints();
	thread maps\_mw2coop_checkpoints::watch_flags();
	thread maps\_mw2coop_rec::start();	// what both players do and where they are, into mw2coop.log
	thread watch_failure();
	if ( god_mode_on() )
		thread god_mode();

	if ( GetDvar( "mw2coop_qa" ) != "" )
	{
		thread maps\_mw2coop_qa::link_probe();
		thread maps\_mw2coop_qa::main();
	}
}

// --------------------------------------------------------------------------------------------
// Third-person bodies: campaign players have no model, so the partner was invisible, or only the
// first-person arms of a scene rig showed where he stood. Each player gets the Spec Ops co-op
// body of the level's uniform (maps\_mw2coop_bodies; the DLL loads the zone that holds it);
// _loadout's UpdateModel, as in Spec Ops, sets it and keeps the current weapon's model attached.
// A player never sees his own body. Precached here, before the level's first wait.

set_bodies()
{
	SetDvar( "mw2coop_transplant", maps\_mw2coop_bodies::transplant_request( level.script ) );
	foreach ( n, player in level.players )
	{
		models = maps\_mw2coop_bodies::player_bodies( level.script, n );
		if ( !isdefined( models ) )
			return;
		PrecacheModel( models[ 0 ] );
		PrecacheModel( models[ 1 ] );
		if ( level.script == "cliffhanger" )
		{
			PrecacheModel( maps\_mw2coop_bodies::rider_model( models[ 0 ] ) );
			PrecacheModel( "mw2coop_pick_up" );
		}
		player.mw2coop_body = models[ 0 ];
		player.mw2coop_head = models[ 1 ];
		player maps\_mw2coop_bodies::give_body( ::body_model );
	}
}

// maps\_loadout::UpdateModel, for the current weapon's model (in the default case, attached to the
// right hand). Cliffhanger's ice picks: in the pistol grip the pick stuck out ahead of the fist
// like a pistol (owner: "turn them upright, right into his hands"): the DLL's upright copy of the
// pick (mw2coop_pick_up, src/transplant.c upright_pick), one in each hand.
attach_weapon_model( weapon_model )
{
	if ( weapon_model == "weapon_ice_picker" && is_campaign_coop() && level.script == "cliffhanger" )
	{
		self attach( "mw2coop_pick_up", "tag_weapon_right", true );
		self attach( "mw2coop_pick_up", "tag_weapon_left", true );
		return;
	}
	self attach( weapon_model, "tag_weapon_right", true );
}

// The left hand's copy of a dual-wield weapon: none for the ice picks, whose upright pair
// attach_weapon_model puts in both hands (a pick ahead of the left fist made four picks, owner).
attach_weapon_model_left( weapon_model )
{
	if ( weapon_model == "weapon_ice_picker" && is_campaign_coop() && level.script == "cliffhanger" )
		return;
	self attach( weapon_model, "tag_weapon_left", true );
}

body_model()
{
	self SetModel( self.mw2coop_body );
	self Attach( self.mw2coop_head, "", true );
}

// --------------------------------------------------------------------------------------------
// God mode for testing: on only when the host sets "mw2coop_god 1" before coopLaunch (the DLL
// sends the value to the client, so both lockstep machines agree; tools/qa_pair.sh sets it).
// Players take no damage, so they are never downed; scripted deaths and mission failures still
// happen. It used to be on unless the dvar was "0", and since the release's .bat launchers were
// replaced by the in-game lobby nothing set "0": every lobby session ran invulnerable.

god_mode_on()
{
	return GetDvar( "mw2coop_god" ) == "1";
}

god_mode()
{
	state( "god mode on (test: mw2coop_god 1)" );
	for ( ;; )
	{
		foreach ( player in level.players )
		{
			if ( !IsAlive( player ) )
				continue;
			player EnableInvulnerability();	// level scripts turn it off at times; put it back every frame
			if ( player.health < player.maxhealth )
				player.health = player.maxhealth;
		}
		wait 0.05;
	}
}

// --------------------------------------------------------------------------------------------
// Messages to the outside: the DLL logs mw2coop_state on both machines and acts on
// mw2coop_cmd on the host only, when its value changes. Every request carries a number that
// keeps counting through restarts and levels (dvar mw2coop_cmd_n): GetTime() alone starts again
// with each restart, and a request sent at the same level time as the last one (a restart from a
// checkpoint, after the same 30 s twice; QA tour 2026-10-22) was the same value and was dropped.

state( text )
{
	SetDvar( "mw2coop_state", text );
}

// The DLL reads mw2coop_cmd every 250 ms, so requests are queued and sent 0.3 s apart.
request( what )
{
	if ( !isdefined( level.mw2coop_requests ) )
	{
		level.mw2coop_requests = [];
		level thread send_requests();
	}
	level.mw2coop_requests[ level.mw2coop_requests.size ] = what;
	level notify( "mw2coop_request" );
}

send_requests()
{
	for ( ;; )
	{
		while ( level.mw2coop_requests.size == 0 )
			level waittill( "mw2coop_request" );
		what = level.mw2coop_requests[ 0 ];
		level.mw2coop_requests = array_remove_index( level.mw2coop_requests, 0 );
		n = GetDvarInt( "mw2coop_cmd_n" ) + 1;
		SetDvar( "mw2coop_cmd_n", n );
		SetDvar( "mw2coop_cmd", what + " @" + GetTime() + "#" + n );
		wait 0.3;
	}
}

// --------------------------------------------------------------------------------------------
// Saves: each PC would write its own save, and loading one would break lockstep. Progress is
// kept as checkpoints (level start points) instead.

no_saves()
{
	waittillframeend;	// maps\_load sets sv_saveOnStartMap after we are called
	SetSavedDvar( "sv_saveOnStartMap", false );
	if ( isdefined( level.flag ) && isdefined( level.flag[ "disable_autosaves" ] ) )
		flag_set( "disable_autosaves" );
}

// --------------------------------------------------------------------------------------------
// Keeping player 2 with player 1

// Start points move player 1 at the end of the first frame. If player 1 is not standing on
// ground yet (a scene, a climb, a vehicle), wait until he is, or until a level script seats
// player 2 somewhere itself.
regroup_after_start()
{
	level endon( "mw2coop_level_end" );
	if ( !isdefined( level.player2 ) )
		return;
	wait 0.1;
	restore_checkpoint_spot();
	for ( waited = 0; waited < 120; waited += 0.25 )
	{
		if ( level.player2 IsLinked() )
			break;
		if ( level.player2 teleport_to_partner( level.player ) )
		{
			state( "regrouped at start point " + level.start_point );
			break;
		}
		wait 0.25;
	}
	if ( waited >= 120 )
		state( "no safe spot next to player 1 within 120 s" );
	// a level placing the two players itself waits for this (maps\_mw2coop_trainer)
	flag_set( "mw2coop_regrouped" );
}

// When a script moves player 1 far in one step (teleport, start point), bring player 2 along.
follow_leader_teleports()
{
	level endon( "mw2coop_level_end" );
	if ( !isdefined( level.player2 ) )
		return;
	wait 0.15;
	last = level.player.origin;
	far_time = 0;
	follow = false;
	for ( ;; )
	{
		wait 0.05;
		now = level.player.origin;
		// a seated or otherwise linked player 2 rides along with whatever holds him; a level
		// whose players go separate ways for a while (the Cliffhanger climb lanes) sets
		// level.mw2coop_no_follow
		if ( DistanceSquared( now, last ) > 512 * 512 && !level.player2 IsLinked() && !isdefined( level.mw2coop_no_follow ) )
			follow = true;
		last = now;
		// player 1 may land in a scene or in the air: follow once he stands somewhere
		if ( follow && !level.player2 IsLinked() && level.player2 teleport_to_partner( level.player ) )
		{
			follow = false;
			state( "followed a teleport of player 1" );
		}

		// Leash: a slow scripted move of player 1 (a blended link, a scene) or simply drifting
		// apart leaves player 2 far behind; after a while bring him over. Only while both are
		// free and up; a level can turn it off with level.mw2coop_no_leash.
		if ( leash_applies() && DistanceSquared( now, level.player2.origin ) > leash_distance() * leash_distance() )
			far_time += 0.05;
		else
			far_time = 0;
		if ( far_time >= leash_time() && level.player2 teleport_to_partner( level.player ) )
		{
			far_time = 0;
			state( "leash: player 2 brought to player 1" );
		}
	}
}

leash_distance()
{
	return 3000;
}

leash_time()
{
	return 8;	// seconds
}

leash_applies()
{
	if ( isdefined( level.mw2coop_no_leash ) && level.mw2coop_no_leash )
		return false;
	if ( isdefined( level.mw2coop_no_follow ) )
		return false;
	if ( level.player IsLinked() || level.player2 IsLinked() )
		return false;
	return !is_downed( level.player ) && !is_downed( level.player2 );
}

// self: the player to move. Tries free spots beside and behind the partner, each with a clear
// line from him and floor at his height. Moves nobody and returns false while the partner is
// not standing on ground (linked, falling, climbing): his origin may be over a drop, and a
// player put there falls (Cliffhanger). Returns true once moved.
teleport_to_partner( partner )
{
	if ( !IsAlive( partner ) || !is_standing( partner ) )
		return false;
	angles = partner GetPlayerAngles();
	yaw = ( 0, angles[ 1 ], 0 );
	forward = AnglesToForward( yaw );
	right = AnglesToRight( yaw );
	offsets = [];
	offsets[ offsets.size ] = right * 48;
	offsets[ offsets.size ] = right * -48;
	offsets[ offsets.size ] = forward * -48;
	offsets[ offsets.size ] = right * 64 + forward * -48;
	offsets[ offsets.size ] = right * -64 + forward * -48;
	offsets[ offsets.size ] = forward * -96;
	offsets[ offsets.size ] = right * 96;
	offsets[ offsets.size ] = right * -96;
	chest = partner.origin + ( 0, 0, 48 );
	best = undefined;
	best_open = -1;
	foreach ( offset in offsets )
	{
		spot = partner.origin + offset;
		// a clear line from the partner (ignoring the partner's own body) ...
		if ( !BulletTracePassed( chest, spot + ( 0, 0, 48 ), false, partner ) )
			continue;
		// ... and floor under the spot, at about the partner's height
		ground = PlayerPhysicsTrace( spot + ( 0, 0, 48 ), spot - ( 0, 0, 96 ) );
		if ( abs( ground[ 2 ] - partner.origin[ 2 ] ) > 40 )
			continue;
		// ... where he fits: a step is free at his feet (Wolverines! start burgertown put him in
		// a spot open at knee height but stuck, QA tour 2026-10-22) ...
		if ( spot_stuck( ground ) )
			continue;
		// ... with room around it: the S.S.D.D. range start had a free spot 48 units left of
		// player 1 wedged between sandbags and the rifle table, open on 3 sides of 8 (owner's
		// screenshot and QA pickups, 2026-10-21). The first open spot wins, else the most open.
		open = open_sides( ground );
		if ( open >= 5 )
		{
			best = ground;
			break;
		}
		if ( open > best_open )
		{
			best = ground;
			best_open = open;
		}
	}
	if ( isdefined( best ) )
	{
		self SetOrigin( best );
		self SetPlayerAngles( yaw );
		return true;
	}
	// nowhere free: share the spot he stands on, the players push apart
	self SetOrigin( partner.origin );
	self SetPlayerAngles( yaw );
	return true;
}

// The player an enemy goes for (maps\_spawner, maps\_load: "player seekers", a favourite enemy
// "player"): the closest one alive and up, as Spec Ops does with
// maps\_utility::get_closest_player_healthy() (that one needs the revive flags, which a campaign
// level may not have yet). Player 1 outside co-op, as the original.
seek_player( origin )
{
	if ( !isdefined( level.player2 ) )
		return level.player;
	best = level.player;
	best_d = undefined;
	foreach ( player in level.players )
	{
		if ( !IsAlive( player ) || is_downed( player ) )
			continue;
		d = DistanceSquared( origin, player.origin );
		if ( !isdefined( best_d ) || d < best_d )
		{
			best = player;
			best_d = d;
		}
	}
	return best;
}

// A scene that hides "the player" from enemies (level.player.ignoreme) hides both players: the
// partner is in the same scene (tools/seek_patch.py). Player 1 alone outside co-op.
players_ignoreme( on )
{
	foreach ( player in level.players )
		player.ignoreme = on;
}

// A scene that sets how well enemies hit "the player" (level.player.attackeraccuracy, e.g. 0 while
// he cannot fight back) or how much they want him (level.player.threatbias) sets it on both: the
// partner is in the same scene.
players_attackeraccuracy( value )
{
	foreach ( player in level.players )
		player.attackeraccuracy = value;
}

players_threatbias( value )
{
	foreach ( player in level.players )
		player.threatbias = value;
}

// A level's intro (the mission title on black) belongs to the mission's first start. A co-op
// restart at a checkpoint, or a campaign continued from one, starts the level at that start point,
// and the intro's own skip for other starts is dev-only code (/# #/ in introscreen_delay): in the
// retail game both players saw the title and stood frozen again after every death (QA tour,
// 2026-10-22). In single player a checkpoint is a saved game, without an intro either.
intro_skipped()
{
	return is_campaign_coop() && !is_default_start();
}

// A level's intro (maps\_introscreen: the mission title on black, the slam zoom) holds player 1
// still and without weapons; it holds every player (QA buttons, 2026-10-21: player 2 walked and
// took a pistol while player 1 was held). Player 1 alone outside co-op.
players_freeze_controls( on )
{
	foreach ( player in level.players )
		player FreezeControls( on );
}

players_disable_weapons()
{
	foreach ( player in level.players )
		player DisableWeapons();
}

players_enable_weapons()
{
	foreach ( player in level.players )
		player EnableWeapons();
}

// In how many of 8 directions a player standing at spot could walk 40 units (at knee height).
// True when a player standing at spot could not take an 8-unit step in any of four directions:
// he would be inside something solid.
spot_stuck( spot )
{
	foreach ( yaw in strtok( "0 90 180 270", " " ) )
	{
		to = spot + AnglesToForward( ( 0, int( yaw ), 0 ) ) * 8 + ( 0, 0, 2 );
		if ( Distance( PlayerPhysicsTrace( spot + ( 0, 0, 2 ), to ), to ) < 0.5 )
			return false;
	}
	return true;
}

open_sides( spot )
{
	open = 0;
	from = spot + ( 0, 0, 18 );
	for ( yaw = 0; yaw < 360; yaw += 45 )
	{
		to = from + AnglesToForward( ( 0, yaw, 0 ) ) * 40;
		if ( Distance( PlayerPhysicsTrace( from, to ), to ) < 1 )
			open++;
	}
	return open;
}

// Free and on ground. IsOnGround() reads 0 for player 2 on both machines in co-op (seen in the
// spawn audit), so ground under the feet is traced too.
is_standing( player )
{
	if ( player IsLinked() )
		return false;
	if ( player IsOnGround() )
		return true;
	below = PlayerPhysicsTrace( player.origin + ( 0, 0, 8 ), player.origin - ( 0, 0, 24 ) );
	return player.origin[ 2 ] - below[ 2 ] < 4;
}

// self: the player to move. teleport_to_partner(), retried for up to timeout seconds (the
// partner may still be getting out of a vehicle or a scene).
teleport_to_partner_when_safe( partner, timeout )
{
	for ( waited = 0; waited < timeout; waited += 0.1 )
	{
		if ( self teleport_to_partner( partner ) )
			return true;
		wait 0.1;
	}
	return false;
}

// --------------------------------------------------------------------------------------------
// Weapons: level scripts give and take weapons on level.player only.

is_downed( player )
{
	return player ent_flag_exist( "coop_downed" ) && player ent_flag( "coop_downed" );
}

give_weapon( player, weapon )
{
	player GiveWeapon( weapon );
	player GiveMaxAmmo( weapon );
	if ( player GetCurrentWeapon() == "none" && is_in_array( player GetWeaponsListPrimaries(), weapon ) )
		player SwitchToWeapon( weapon );
}

// Player 2 starts with exactly player 1's weapons.
copy_weapons( to, from )
{
	foreach ( weapon in to GetWeaponsListAll() )
		if ( !from HasWeapon( weapon ) )
			to TakeWeapon( weapon );
	foreach ( weapon in from GetWeaponsListAll() )
		if ( !to HasWeapon( weapon ) )
			give_weapon( to, weapon );
	current = from GetCurrentWeapon();
	if ( current != "none" && to HasWeapon( current ) )
		to SwitchToWeapon( current );
}

// A loadout gives weapons and then switches to one (airport: m4_grenadier, m240, then
// SwitchToWeapon m240); while player 1 raises it his current weapon is still the first one, so
// copy_weapons() put player 2 on the M4 (owner, 2026-10-21: "the client got a rifle instead of
// the machine gun"). Player 2 holds whatever player 1 settles on during the first seconds.
follow_first_weapon_switch()
{
	level endon( "mw2coop_level_end" );
	asked = level.player2 GetCurrentWeapon();
	for ( t = 0; t < 4; t += 0.1 )
	{
		wait 0.1;
		current = level.player GetCurrentWeapon();
		if ( current != "none" && current != asked && level.player2 HasWeapon( current ) && level.player2 GetCurrentWeapon() != current )
		{
			asked = current;
			level.player2 SwitchToWeapon( current );
			state( "player 2 raises " + current + " as player 1 does" );
		}
	}
}

// Level loadouts (maps\_loadout) set first-person hands on level.player only. Without hands the
// client's own player cannot draw a weapon: the engine raises "no viewmodel set" and drops the
// co-op session. Player 2 always wears player 1's hands, also when a level changes them.
sync_viewmodel()
{
	hands = level.player GetViewModel();
	if ( hands != "" && level.player2 GetViewModel() != hands )
		level.player2 SetViewModel( hands );
}

// Weapons each player's own scene takes and gives back (the Cliffhanger ice picks: the climb
// takes them from the climbing player and returns them at the top): never taken from player 2
// because player 1 lost them, and not handed to player 2 while he is in such a scene himself
// (linked). Playtest 2026-10-23: player 1 started climbing and player 2 lost his picks; player 1
// reached the top and player 2, still on the wall, got the picks back and fell.
own_weapon_busy( weapon )
{
	return is_in_array( level.mw2coop_own_weapons, weapon ) && level.player2 IsLinked();
}

sync_weapons()
{
	level endon( "mw2coop_level_end" );
	if ( !isdefined( level.player2 ) )
		return;
	sync_viewmodel();	// before any weapon reaches player 2
	wait 0.15;	// after start points hand out weapons
	sync_viewmodel();
	copy_weapons( level.player2, level.player );
	state( "player 2 weapons copied from player 1" );
	thread follow_first_weapon_switch();
	prev1 = level.player GetWeaponsListAll();
	prev2 = level.player2 GetWeaponsListAll();
	for ( ;; )
	{
		wait 0.25;
		sync_viewmodel();
		now1 = level.player GetWeaponsListAll();
		now2 = level.player2 GetWeaponsListAll();
		// the revive system takes and returns weapons of a downed player: leave it alone
		if ( level.mw2coop_weapon_sync != "own" && !is_downed( level.player ) && !is_downed( level.player2 ) )
		{
			foreach ( weapon in now1 )
				if ( !is_in_array( prev1, weapon ) && !level.player2 HasWeapon( weapon ) && !own_weapon_busy( weapon ) )
					give_weapon( level.player2, weapon );
			if ( level.mw2coop_weapon_sync == "both" )
				foreach ( weapon in now2 )
					if ( !is_in_array( prev2, weapon ) && !level.player HasWeapon( weapon ) )
						give_weapon( level.player, weapon );
			foreach ( weapon in prev1 )
				if ( !is_in_array( now1, weapon ) && level.player2 HasWeapon( weapon ) && !is_in_array( level.mw2coop_own_weapons, weapon ) )
					level.player2 TakeWeapon( weapon );
		}
		prev1 = level.player GetWeaponsListAll();
		prev2 = level.player2 GetWeaponsListAll();
	}
}

// --------------------------------------------------------------------------------------------
// Checkpoints: the furthest progress flag reached sets the "start" dvar, so a restart resumes
// from that start point on both machines (the dvar is set by this script, so it matches).

track_checkpoints()
{
	if ( !isdefined( level.mw2coop_checkpoints ) )
		return;
	wait 0.1;	// level.start_point is chosen at the end of the first frame
	foreach ( index, cp in level.mw2coop_checkpoints )
		if ( isdefined( level.start_point ) && level.start_point == cp.start )
			level.mw2coop.checkpoint = index;
	foreach ( index, cp in level.mw2coop_checkpoints )
		thread checkpoint_wait( index, cp );
}

checkpoint_wait( index, cp )
{
	level endon( "mw2coop_level_end" );
	while ( !isdefined( level.flag[ cp.flag ] ) )
		wait 0.05;
	flag_wait( cp.flag );
	if ( index <= level.mw2coop.checkpoint || level.mw2coop.failing )
		return;
	level.mw2coop.checkpoint = index;
	SetDvar( "start", cp.start );
	state( "checkpoint " + cp.start );
	request( "checkpoint " + level.script + " " + cp.start );	// the host saves it for mw2coop_continue
	foreach ( player in level.players )
		player thread checkpoint_hud();
}

// Checkpoints for every level: a section of the level begins (the start point "name": its section
// function in maps\_load::handle_starts, or the level's own section handler, patched): it becomes
// the co-op checkpoint, so a failed mission restarts there instead of at the level's beginning, as
// a single player autosave would let him go on. Only forward, only real start points (not the
// developers' test starts, start_is_dev), and not the start point the level began at.
// leader: the player who got there (a checkpoint flag's trigger he touched); undefined when not
// known.
section_reached( name, leader )
{
	if ( !is_campaign_coop() || !isdefined( level.mw2coop ) || level.mw2coop.failing || level.mw2coop.completing )
		return;
	index = undefined;
	foreach ( i, start in level.start_functions )
		if ( start[ "name" ] == name )
			index = i;
	if ( !isdefined( index ) || index <= level.mw2coop.section )
		return;
	level.mw2coop.section = index;
	if ( start_is_dev( name ) || ( isdefined( level.start_point ) && level.start_point == name ) )
		return;
	SetDvar( "start", name );
	state( "checkpoint " + name + " (section " + index + ")" );
	if ( !isdefined( leader ) )
		leader = standing_leader();
	spot = checkpoint_spot( leader );
	SetDvar( "mw2coop_cp_start", name );
	SetDvar( "mw2coop_cp_pos", spot );
	request( "checkpoint " + level.script + " " + name + " " + spot );	// the host saves it for mw2coop_continue
	foreach ( player in level.players )
		player thread checkpoint_hud();
	bring_partner_to( leader );
}

// --------------------------------------------------------------------------------------------
// Where a checkpoint was reached. Some start points do not place the player (Cliffhanger "c4": its
// start function's teleport is commented out), so a restart there left both players at the level's
// first spawn (playtest 2026-10-23: restart at c4 put them back at the cave). The spot of the player
// who got there is kept with the checkpoint (dvars mw2coop_cp_start, mw2coop_cp_pos, and the host's
// save) and used when the start point leaves the player where the level spawns him.
// Encoded as a plain name for the start packet: x_y_z_yaw, "m" for minus ("m4131_m25410_998_61").

checkpoint_spot( leader )
{
	player = leader;
	if ( !isdefined( player ) || !is_standing( player ) )
	{
		player = undefined;
		foreach ( p in level.players )
			if ( !isdefined( player ) && is_standing( p ) )
				player = p;
	}
	if ( !isdefined( player ) )
		return "";
	angles = player GetPlayerAngles();
	return spot_int( player.origin[ 0 ] ) + "_" + spot_int( player.origin[ 1 ] ) + "_" + spot_int( player.origin[ 2 ] ) + "_" + spot_int( angles[ 1 ] );
}

spot_int( value )
{
	value = int( value );
	if ( value < 0 )
		return "m" + ( 0 - value );
	return "" + value;
}

spot_value( text )
{
	if ( GetSubStr( text, 0, 1 ) == "m" )
		return 0 - int( GetSubStr( text, 1 ) );
	return int( text );
}

// At the start: player 1 to the checkpoint's spot when the start point left him at the level's
// spawn. Both machines read the same dvars (set by the scripts, or by the host's start packet).
restore_checkpoint_spot()
{
	if ( !isdefined( level.start_point ) || level.start_point == "default" || GetDvar( "mw2coop_cp_start" ) != level.start_point )
		return;
	parts = StrTok( GetDvar( "mw2coop_cp_pos" ), "_" );
	if ( parts.size != 4 )
		return;
	spot = ( spot_value( parts[ 0 ] ), spot_value( parts[ 1 ] ), spot_value( parts[ 2 ] ) );
	spawns = GetEntArray( "info_player_start", "classname" );
	if ( !spawns.size || level.player IsLinked() )
		return;
	if ( DistanceSquared( level.player.origin, spawns[ 0 ].origin ) > 128 * 128 )
		return;	// the start point placed him itself
	if ( DistanceSquared( level.player.origin, spot ) < 512 * 512 )
		return;
	level.player SetOrigin( spot );
	level.player SetPlayerAngles( ( 0, spot_value( parts[ 3 ] ), 0 ) );
	state( "start point " + level.start_point + " left player 1 at the level's spawn: moved to the checkpoint's spot " + spot );
}

// A level's end goes on to the next mission only from its default start (No Russian, The Enemy of
// My Enemy, Second Sun, Whiskey Hotel: "DEVELOPER: END OF SCRIPTED LEVEL" otherwise); a co-op
// checkpoint is a start point, so in co-op the end always goes on (playtest 2026-10-23: No Russian
// restarted at "tarmac" never ended).
ending_goes_on()
{
	return is_default_start() || is_campaign_coop();
}

// --------------------------------------------------------------------------------------------
// Owner, 2026-10-23: whoever reaches a checkpoint first, the other player is brought to him.
// Only a free player is moved: one linked to something (a vehicle, a scene, the Cliffhanger
// climb) stays where the level put him, and a downed one is left to the revive system.

player_label( player )
{
	if ( player == level.player )
		return "player 1";
	return "player 2";
}

// A player stuck off the ground (not linked, not standing: fallen into the rock, left on a
// ledge) while the other stands: the standing one leads. Else unknown.
standing_leader()
{
	if ( !isdefined( level.player2 ) )
		return undefined;
	foreach ( player in level.players )
	{
		other = other_player( player );
		if ( is_standing( player ) && !other IsLinked() && !is_standing( other ) )
			return player;
	}
	return undefined;
}

bring_partner_to( leader )
{
	if ( !isdefined( leader ) || !isdefined( level.player2 ) )
		return;
	other = other_player( leader );
	if ( !IsAlive( other ) || other IsLinked() || is_downed( other ) || is_downed( leader ) )
		return;
	if ( DistanceSquared( other.origin, leader.origin ) < 400 * 400 && is_standing( other ) )
		return;
	other thread brought_to( leader );
}

// self: the player to move
brought_to( leader )
{
	level endon( "mw2coop_level_end" );
	self endon( "death" );
	if ( self teleport_to_partner_when_safe( leader, 10 ) )
		state( player_label( self ) + " brought to " + player_label( leader ) + " at the checkpoint" );
	else
		state( "no safe spot next to " + player_label( leader ) + " for " + player_label( self ) );
	self notify( "mw2coop_brought" );
}

// The developers' test starts: they skip ahead or leave the level half set up (the knife fight's
// later steps only start from the step before them: wakefast froze the game with an endless
// anim_loop_packet error). Not used as checkpoints; the level tour skips them too.
start_is_dev( name )
{
	foreach ( word in strtok( "debug test tweak nofail no_game alt_ending ending_ diskdrive fakerpg so_ free black body_throw noslowmo", " " ) )
		if ( IsSubStr( name, word ) )
			return true;
	foreach ( word in strtok( "wakefast turnbuckle gloat gun_drop crawl gun_kick wounded pullout kill price_wakeup walkoff jump e3", " " ) )
		if ( name == word )
			return true;
	return false;
}

checkpoint_hud()
{
	hud = NewClientHudElem( self );
	hud.x = 0;
	hud.y = -60;
	hud.alignX = "center";
	hud.alignY = "middle";
	hud.horzAlign = "center";
	hud.vertAlign = "middle";
	hud.font = "objective";
	hud.fontscale = 1.5;
	hud.foreground = true;
	hud.hidewheninmenu = true;
	hud.alpha = 1;
	hud SetText( &"SCRIPT_CHECKPOINT" );
	wait 2;
	hud FadeOverTime( 1 );
	hud.alpha = 0;
	wait 1;
	hud Destroy();
}

// --------------------------------------------------------------------------------------------
// Failure: both players down (maps\_coop revive system) or a scripted mission failure.

watch_failure()
{
	level endon( "mw2coop_level_end" );
	foreach ( player in level.players )
		player thread fail_on_death();
	while ( !isdefined( level.flag[ "coop_alldowned" ] ) )
		wait 0.05;
	flag_wait( "coop_alldowned" );
	// own thread: mission_failed() sends mw2coop_level_end, which would end this thread
	level thread mission_failed();
}

// A player's difficulty for maps\_gameskill::updateGameSkill(): in a co-op campaign the one the
// host picked (mw2coop_skill, sent to the client with coopLaunch, src/progress.c), for both
// players on both machines; the game took each player's from his own profile, so the two could
// play different ones and the level the higher. Elsewhere, or without a pick, the profile's.
player_gameskill()
{
	skill = GetDvar( "mw2coop_skill" );
	if ( is_campaign_coop() && ( skill == "0" || skill == "1" || skill == "2" || skill == "3" ) )
		return int( skill );
	return self maps\_utility::get_player_gameskill();
}

// Friendly fire between the players: the host's lobby setting (mw2coop_ff, "0" off; sent to the
// client with coopLaunch). On, the game's own co-op lets them hurt each other (QA pvp: a burst of
// M4 fire took the partner from 100 to 2). Off, g_friendlyfireDamageScale 0 leaves 1 damage a
// hit (QA pvp0: 100 to 92), which is given back.
friendly_fire_setting()
{
	if ( GetDvar( "mw2coop_ff" ) != "0" )
		return;
	SetSavedDvar( "g_friendlyfireDamageScale", 0 );
	foreach ( player in level.players )
		player thread give_back_partner_damage();
	state( "friendly fire between players off" );
}

// self: a player. Damage the other player did to him comes back.
give_back_partner_damage()
{
	self endon( "death" );
	level endon( "mw2coop_level_end" );
	for ( ;; )
	{
		self waittill( "damage", amount, attacker );
		if ( !isdefined( attacker ) || !IsPlayer( attacker ) || attacker == self )
			continue;
		self.health = int( min( self.maxhealth, self.health + amount ) );
	}
}

// A field of view only one player should get (the Predator's UAV view sets cg_fov for both
// screens): mw2coop_fov_pilot names that player ("1" or "2", "" when it ends), and the DLL keeps
// the other player's own field of view on his machine (src/fov.c).
fov_pilot( player )
{
	if ( !is_campaign_coop() )
		return;
	if ( !isdefined( player ) )
		SetDvar( "mw2coop_fov_pilot", "" );
	else if ( isdefined( level.player2 ) && player == level.player2 )
		SetDvar( "mw2coop_fov_pilot", "2" );
	else
		SetDvar( "mw2coop_fov_pilot", "1" );
}

// maps\_coop calls resetSkill() on every down and revive (so the player still up gets Spec Ops'
// small buff). It also puts both players' threatbias and attackeraccuracy back to the
// difficulty's values, which in the campaign undid a scene's own (Loose Ends' accuracy ramp,
// Team Player's and The Only Easy Day's threatbias; QA sceneskill, 2026-10-22). They are kept.
reset_skill_keep_scene()
{
	if ( !is_campaign_coop() )
	{
		maps\_gameskill::resetSkill();
		return;
	}
	threatbias = [];
	accuracy = [];
	foreach ( n, player in level.players )
	{
		threatbias[ n ] = player.threatbias;
		accuracy[ n ] = player.attackeraccuracy;
	}
	maps\_gameskill::resetSkill();
	foreach ( n, player in level.players )
	{
		player.threatbias = threatbias[ n ];
		if ( !player ent_flag( "player_zero_attacker_accuracy" ) )
			player.attackeraccuracy = accuracy[ n ];
	}
}

// A player killed outright (a scripted Kill(), as when he steps off Cliffhanger's cave ledge,
// owner 2026-10-22) is not downed and cannot be revived: the mission fails, as his death does in
// single player. Without this the other player played on beside a dead partner.
fail_on_death()
{
	level endon( "mw2coop_level_end" );
	self waittill( "death" );
	if ( isdefined( level.player2 ) && self == level.player2 )
		state( "player 2 died" );
	else
		state( "player 1 died" );
	level thread mission_failed();
}

// Replaces MissionFailed() in a co-op campaign session.
mission_failed()
{
	if ( level.mw2coop.failing || level.mw2coop.completing )
		return;
	level.mw2coop.failing = true;
	level notify( "mw2coop_level_end" );
	flag_set( "special_op_terminated" );
	state( "mission failed (deadquote '" + GetDvar( "ui_deadquote" ) + "'), restart at '" + GetDvar( "start" ) + "'" );
	foreach ( player in level.players )
	{
		player EnableInvulnerability();
		player FreezeControls( true );
		overlay = create_client_overlay( "black", 0, player );
		overlay FadeOverTime( 2 );
		overlay.alpha = 1;
	}
	wait 3;
	request( "restart" );	// the host runs fast_restart; the engine sends coopRestart to the client
}

// --------------------------------------------------------------------------------------------
// Mission end: replaces maps\_endmission::_nextmission() in a co-op campaign session.
// ChangeLevel() does nothing in a co-op session: its exit handler (rva 0x170e60) returns early
// when gameMode is "so". The host's DLL starts the next mission the way the lobby does
// (coop_mapName + coopLaunch, which also sends the client there). The game's records
// (highestMission, difficulty) are local-player profile data and are not written.
// Not handled yet: _nextmission()'s cliffhanger -> airport special case.

next_level_name()
{
	settings = level.missionSettings;
	index = settings maps\_endmission::getLevelIndex( level.script );
	// This build's _nextmission() completes airport right after cliffhanger ("always skip
	// airport for RUSSIAN PC BUILD", with no build check). The owner wants No Russian played:
	// co-op always goes on with airport.
	if ( !isdefined( index ) || index + 1 >= settings.levels.size )
		return "";
	return settings maps\_endmission::getLevelName( index + 1 );
}

mission_complete()
{
	if ( level.mw2coop.completing || level.mw2coop.failing )
		return;
	level.mw2coop.completing = true;
	level notify( "mw2coop_level_end" );
	flag_set( "special_op_terminated" );
	level notify( "nextmission" );
	level.nextmission = true;
	foreach ( player in level.players )
		player EnableInvulnerability();
	next = next_level_name();
	SetDvar( "start", "" );
	write_records();
	state( "mission complete " + level.script + ", next '" + next + "'" );
	request( "nextmap " + next );	// the host saves it as progress and launches it
}

// The campaign records _nextmission() writes in single player, on each machine into its own
// player's profile: the mission's best difficulty and the furthest mission (setLevelCompleted:
// a mission after one never completed is not counted, as in single player), the campaign
// percentage, the profile save, and the mission's achievements. Profile data is local and
// changes no game state, so the two lockstep machines may differ here. Endgame (af_chase) is
// recorded with the ending, as _nextmission( true ) does.
//
// The profile methods only answer for this machine's own player (on the client, level.player is
// the host's player and reads an empty profile), and _endmission works on level.player: it is
// that player for the few calls below, which never wait, so no other script sees the swap.
write_records()
{
	settings = level.missionSettings;
	script = level.script;
	if ( script == "af_chase" )
		return;
	if ( script == "ending" )
		script = "af_chase";
	index = settings maps\_endmission::getLevelIndex( script );
	if ( !isdefined( index ) )
		return;
	own = undefined;
	foreach ( player in level.players )
		if ( player GetLocalPlayerProfileData( "missionHighestDifficulty" ) != "" )
			own = player;
	if ( !isdefined( own ) )
		return;
	before = own GetLocalPlayerProfileData( "highestMission" ) + " " + own GetLocalPlayerProfileData( "missionHighestDifficulty" );
	player1 = level.player;
	level.player = own;
	settings maps\_endmission::setLevelCompleted( index );
	maps\_endmission::updateSpPercent();
	UpdateGamerProfile();
	if ( settings maps\_endmission::hasAchievement( index ) )
		maps\_utility::giveachievement_wrapper( settings maps\_endmission::getAchievement( index ) );
	level.player = player1;
	after = own GetLocalPlayerProfileData( "highestMission" ) + " " + own GetLocalPlayerProfileData( "missionHighestDifficulty" );
	who = "player 1";
	if ( own != level.player )
		who = "player 2";
	state( "records " + script + " (index " + index + "), " + who + ": highest mission and difficulties " + before + " -> " + after );
	wait 0.3;	// the DLL reads mw2coop_state every 250 ms
}

// --------------------------------------------------------------------------------------------
// Vehicles: level scripts seat player 1 only. These seat the partner on a free seat of the
// same vehicle (an AI seat the vehicle has not filled), linked the way the level links
// player 1, and take him out next to player 1 again.

// self: vehicle. A sittag no rider uses, rear seats first; skip_tag: player 1's seat.
free_seat_tag( skip_tag )
{
	if ( !isdefined( self.vehicletype ) || !isdefined( level.vehicle_aianims ) || !isdefined( level.vehicle_aianims[ self.vehicletype ] ) )
		return undefined;
	seats = level.vehicle_aianims[ self.vehicletype ];
	for ( pos = seats.size - 1; pos > 0; pos-- )	// never the driver (0)
	{
		if ( !isdefined( seats[ pos ].sittag ) || ( isdefined( skip_tag ) && seats[ pos ].sittag == skip_tag ) )
			continue;
		if ( isdefined( self.usedPositions ) && isdefined( self.usedPositions[ pos ] ) && self.usedPositions[ pos ] )
			continue;
		if ( IsSubStr( seats[ pos ].sittag, "turret" ) )
			continue;
		self.usedPositions[ pos ] = true;	// keep AI riders off it
		self.mw2coop_seat_pos = pos;
		return seats[ pos ].sittag;
	}
	return undefined;
}

// The player on a vehicle turret shared by two players (a level sets level.mw2coop_gunner).
vehicle_gunner()
{
	if ( isdefined( level.mw2coop_gunner ) && IsAlive( level.mw2coop_gunner ) )
		return level.mw2coop_gunner;
	return level.player;
}

other_player( player )
{
	if ( player == level.player )
		return level.player2;
	return level.player;
}

// self: vehicle. Returns the seat entity (pass it to partner_leaves_seat() or release_seat()).
//   skip_tag: player 1's seat (default tag_passenger; "" skips none); weapons: usable on the seat.
seat_partner_in_vehicle( player, right, left, up, down, skip_tag, weapons )
{
	if ( !isdefined( player ) || !IsAlive( player ) )
		return undefined;
	if ( !isdefined( right ) )
		right = 90;
	if ( !isdefined( left ) )
		left = 90;
	if ( !isdefined( up ) )
		up = 40;
	if ( !isdefined( down ) )
		down = 40;
	if ( !isdefined( skip_tag ) )
		skip_tag = "tag_passenger";

	tag = self free_seat_tag( skip_tag );
	// a level can name a squadmate who gives up his seat (level.mw2coop_spare_rider)
	evicted = "";
	if ( !isdefined( tag ) && isdefined( level.mw2coop_spare_rider ) )
	{
		evicted = " spare " + level.mw2coop_spare_rider + ": " + self evict_rider( level.mw2coop_spare_rider );
		tag = self free_seat_tag( skip_tag );
	}
	seat = spawn_tag_origin();
	seat.mw2coop_vehicle = self;
	seat.mw2coop_arcs = [];
	seat.mw2coop_arcs[ 0 ] = right;
	seat.mw2coop_arcs[ 1 ] = left;
	seat.mw2coop_arcs[ 2 ] = up;
	seat.mw2coop_arcs[ 3 ] = down;
	seat.mw2coop_weapons = isdefined( weapons ) && weapons;
	if ( isdefined( tag ) )
	{
		seat.mw2coop_pos = self.mw2coop_seat_pos;
		// same offset as maps\_utility::player_rides_shotgun_in_humvee() uses on tag_passenger
		seat LinkTo( self, tag, ( -5, 10, -34 ), ( 0, 0, 0 ) );
		state( "partner seated on " + tag + evicted );
	}
	else
	{
		seat LinkTo( self, "tag_body", ( -50, 0, 10 ), ( 0, 0, 0 ) );
		state( "partner: no free seat (" + self seats_text() + ")" + evicted + ", riding in the back" );
	}
	seat link_to_seat( player );
	return seat;
}

// self: seat. Puts player on it (also used when two players swap places).
link_to_seat( player )
{
	self.mw2coop_player = player;
	player AllowCrouch( false );
	player AllowProne( false );
	if ( self.mw2coop_weapons )
		player EnableWeapons();
	else
		player DisableWeapons();
	player PlayerLinkTo( self, "tag_origin", 0.8, self.mw2coop_arcs[ 0 ], self.mw2coop_arcs[ 1 ], self.mw2coop_arcs[ 2 ], self.mw2coop_arcs[ 3 ] );
}

// self: seat. The player gets off where he is; the seat is freed.
release_seat()
{
	player = self.mw2coop_player;
	if ( isdefined( self.mw2coop_vehicle ) && isdefined( self.mw2coop_pos ) && isdefined( self.mw2coop_vehicle.usedPositions ) )
		self.mw2coop_vehicle.usedPositions[ self.mw2coop_pos ] = false;
	if ( isdefined( player ) && IsAlive( player ) )
	{
		org = player.origin;
		player Unlink();
		player SetOrigin( org );
		player EnableWeapons();
		player AllowCrouch( true );
		player AllowProne( true );
	}
	self Delete();
}

// self: seat from seat_partner_in_vehicle().
partner_leaves_seat( delay )
{
	if ( isdefined( delay ) )
		wait delay;
	player = self.mw2coop_player;
	if ( isdefined( self.mw2coop_vehicle ) && isdefined( self.mw2coop_pos ) && isdefined( self.mw2coop_vehicle.usedPositions ) )
		self.mw2coop_vehicle.usedPositions[ self.mw2coop_pos ] = false;
	if ( isdefined( player ) && IsAlive( player ) )
	{
		player Unlink();
		player EnableWeapons();
		player AllowCrouch( true );
		player AllowProne( true );
		player thread teleport_to_partner_when_safe( other_player( player ), 5 );
	}
	self Delete();
}

// self: vehicle. "type: pos=tag used ..." for the log.
seats_text()
{
	if ( !isdefined( self.vehicletype ) )
		return "no vehicletype";
	if ( !isdefined( level.vehicle_aianims ) || !isdefined( level.vehicle_aianims[ self.vehicletype ] ) )
		return self.vehicletype + ": no seat anims";
	text = self.vehicletype + ":";
	seats = level.vehicle_aianims[ self.vehicletype ];
	for ( pos = 0; pos < seats.size; pos++ )
	{
		tag = "?";
		if ( isdefined( seats[ pos ].sittag ) )
			tag = seats[ pos ].sittag;
		used = isdefined( self.usedPositions ) && isdefined( self.usedPositions[ pos ] ) && self.usedPositions[ pos ];
		text += " " + pos + "=" + tag;
		if ( used )
			text += "*";
		if ( isdefined( self.riders ) )
			foreach ( guy in self.riders )
				if ( isdefined( guy ) && isdefined( guy.vehicle_position ) && guy.vehicle_position == pos )
					text += "(" + rider_name( guy ) + ")";
	}
	return text;
}

rider_name( guy )
{
	if ( isdefined( guy.animname ) )
		return guy.animname;
	if ( isdefined( guy.script_noteworthy ) )
		return guy.script_noteworthy;
	if ( isdefined( guy.targetname ) )
		return guy.targetname;
	return "ai";
}

// self: vehicle. Removes the rider with this name (a squadmate the level does not need)
// so a player can take his seat. The vehicle frees the seat when the rider is gone
// (maps\_vehicle_aianim::guy_deathhandle); done here too in case that runs later.
evict_rider( animname )
{
	if ( !isdefined( self.riders ) )
		return "no riders";
	names = "";
	foreach ( guy in self.riders )
	{
		if ( isdefined( guy ) )
			names += rider_name( guy ) + " ";
		// animname, or the spawner's script_noteworthy while spawn functions have not run yet
		if ( !isdefined( guy ) || rider_name( guy ) != animname )
			continue;
		pos = guy.vehicle_position;
		self.riders = array_remove( self.riders, guy );
		if ( isdefined( guy.magic_bullet_shield ) )
			guy stop_magic_bullet_shield();
		guy Delete();
		if ( isdefined( pos ) && isdefined( self.usedPositions ) )
			self.usedPositions[ pos ] = false;
		return "removed";
	}
	return "not among " + names;
}

// Campaign players have no body model (Spec Ops gives them coop_body_* models, which only the
// so_* zones hold), so there is no "tag_origin" on them for effects or links. self: player.
// Returns a tag_origin model that follows him, for code that plays effects on a player's tag.
player_tag_ent()
{
	if ( !isdefined( self.mw2coop_tag_ent ) )
	{
		ent = spawn_tag_origin();
		ent.origin = self.origin;
		ent.angles = self.angles;
		ent LinkTo( self );
		self.mw2coop_tag_ent = ent;
	}
	return self.mw2coop_tag_ent;
}
