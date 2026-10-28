// mw2coop QA: scripted checks that run inside the game on both PCs (lockstep, so they must be
// deterministic). Chosen before launch with the dvar mw2coop_qa on BOTH machines:
//   intro    - player 2 joins player 1 at the range, same weapons, not player 1's rifle
//   restart  - reach a checkpoint, down both players, expect a restart at that checkpoint
//   end      - (launch with "start end") run the ending, expect both machines on the next mission
//   resume   - (qa_pair.sh resume @continue) both machines start at the saved checkpoint
//   god      - with god mode on, heavy damage downs neither player
//   ride     - (invasion) player 2 rides in player 1's vehicle and gets out with him
//   turret   - (roadkill, start "ride") hand the Humvee turret from player 1 to player 2
//   board    - (roadkill, start "convoy") first to board is the gunner, the other rides, swaps
//   rideend  - (roadkill, start "ride_later") both get off at the end of the ride and the crash
//   rkintro  - (roadkill, default start) both players in Shepherd's scene, together afterwards
//   leash    - player 2 far away comes back to player 1
//   snow     - (cliffhanger, start "snowmobile") own snowmobiles; the ending waits for player 2
//   snowpick - (cliffhanger, start "icepick") player 2 takes player 1's snowmobile first
//   climb    - (cliffhanger, default start) both players climb the ice wall on their own lanes,
//              make the big jump, Soap catches both, both climb to the top (bot input)
//   spawn    - (any map/start) where player 2 is during the first 12 s
//   takedown - (favela, default start) both ride the opening car (player 2 on the back seat),
//              both duck before Makarov's shots, both get out next to each other
//   takedown2 - same, but player 2 does not duck: Makarov hits him, not player 1
//   crashsite - (dcburning, start "crash") the same crash site at the end of dcburning
//   dcemp    - (dcemp, default start) both wake up in the crashed Black Hawk, both get a rifle,
//              both float at the ISS, both come back to the wreck and are let out
//   street   - (dcemp, start "street") the falling helicopters: the street is safe only once both
//              players have hidden (the bot moves them into the hiding place)
//   nrend    - (airport, start "escape") the ending at the van: Makarov shoots both players
//   nrend2   - same, player 1 at the van's door (the close ending), player 2 a little away
//   knife    - (ending, start "wakeup") the knife fight: both wake up, player 1 strikes Shepherd
//              and plays the scene, player 2 goes down and watches; knife2: player 2 strikes
//   oilrig   - (oilrig, default start) both ride an SDV underwater, both surface; player 1 does
//              the stealth kill, then both are pulled out of the water. oilrig2: player 2 kills
//   loadout  - (any map) after the level's loadout both players hold the same weapon
//   cover    - (airport, start "stairs") player 2 fires at Makarov's squad: the cover is blown,
//              Makarov shoots both players and the mission fails for both
//   lineoffire - (airport, start "intro", MW2COOP_GOD=0) player 2 walks into the squad's line of
//              fire in the lobby: he dies, not player 1, and the mission fails for both
//   bodyview - (any map) both players 220 units apart, frames of each other standing, player 2
//              stepping sideways and backing off (the body's whole figure in motion)
//   boat     - (af_chase, default start) the Endgame boat chase: player 1 boards and drives,
//              player 2 rides as the gunner beside Price; the bot drives after the enemy boat.
//              boat2: player 2 boards first and drives
//   buttons  - (any map) real input: each player's own machine presses Forward and Use (press()),
//              he walks forward and the server sees his Use button, not the other player's
//   pickups  - (trainer; start "", "frag" or "pit") each player takes his own weapons off the
//              tables (looks at them, presses Use) and walks up to the frag box (Forward)
//   bay      - (trainer; a range start: "", timed_ads, bullet_penetration, frag) both players
//              stand inside the sandbag bay and can move
//   introfreeze - (a map's default start with an intro title) under the title neither player
//              moves on a real Forward press; afterwards both walk
//   tour     - (any map, default start) every playable section of the level (its start points), one
//              after another through restarts: both up, not stuck, both can walk, player 1 runs to
//              the two nearest story triggers and player 2 follows; both frames saved quietly at
//              each stop (mw2coop\shots, src/shot.c)
//   betrayal - (estate, start "ending"; MW2COOP_GOD=0, god mode swallows Shepherd's shot) Loose
//              Ends' ending: both players go through Ghost's drag, the LZ, Shepherd's shot (it hits
//              both), the body toss and the fire as Roach, at his place
//   aimassist - (native split screen, a map with enemies; [native] p1pad=<n>) player 2's pad aim
//              assist (src/native.c aim_assist), his pad pressed through the dvar mw2coop_qa_pad2:
//              an enemy 400 units ahead; aiming down the sight 6 degrees beside it turns onto it;
//              a steady turn across it slows over it; a strafe past it keeps nearer with assist
//   bodycam  - (native split screen) player 1 looks at player 2, who is turned four ways and then
//              by his pad: player 2's body must face where his camera looks; the composed picture
//              of each (both halves) is saved through the dvar mw2coop_qa_frame
//   glyphs   - (trainer, native split screen, [native] p1pad=<n>) each player gets a hint naming a
//              key ("[{+gostand}]"): player 2 on a pad sees the button's picture, player 1 the key;
//              the composed picture is saved as "glyphs" (src/padglyphs.c)
// Results go to the dvar mw2coop_qa_log, which the DLL writes to mw2coop.log on each PC.
#include common_scripts\utility;
#include maps\_utility;

main()
{
	// a scenario that ends the mission marks itself done, so the next map does not rerun it
	if ( GetDvar( "mw2coop_qa_phase" ) == "done" )
		return;
	level.mw2coop_qa_n = 0;
	maps\_mw2coop::state( "qa scenario " + GetDvar( "mw2coop_qa" ) + " started" );
	wait 1.5;
	maps\_mw2coop::state( "qa running" );	// let start points, regroup and weapon sync run
	switch ( GetDvar( "mw2coop_qa" ) )
	{
		case "hidep2":
			wait 5;
			level.player2 Hide();
			angles_log();
			break;
		case "hidep1":
			wait 5;
			level.player Hide();
			angles_log();
			break;
		case "angles":
			angles_log();
			break;
		case "walk50":
			walk50();
			break;
		case "p1far":
			p1far();
			break;
		case "p1front":
			p1front();
			break;
		case "face":
			face();
			break;
		case "gunface":
			gunface();
			break;
		case "hudtest":
			hudtest();
			break;
		case "weapsw":
			weapsw();
			break;
		case "intro":
			intro();
			break;
		case "padwalk":
			padwalk();
			break;
		case "restart":
			restart();
			break;
		case "end":
			ending();
			break;
		case "resume":
			resume();
			break;
		case "god":
			god();
			break;
		case "downs":
			downs();
			break;
		case "sceneskill":
			sceneskill();
			break;
		case "skill":
			skill();
			break;
		case "flares":
			flares();
			break;
		case "swaphint":
			swaphint();
			break;
		case "pilotfov":
			pilotfov();
			break;
		case "pvp":
			pvp();
			break;
		case "cpcheck":
			cpcheck();
			break;
		case "cprestart":
			cprestart();
			break;
		case "ride":
			ride();
			break;
		case "seatview":
			seatview();
			break;
		case "turret":
			turret();
			break;
		case "board":
			board();
			break;
		case "rideend":
			rideend();
			break;
		case "rkintro":
			rkintro();
			break;
		case "leash":
			leash();
			break;
		case "snow":
			snow();
			break;
		case "snowview":
			snowview();
			break;
		case "snowpick":
			snowpick();
			break;
		case "spawn":
			spawn_audit();
			break;
		case "takedown":
		case "takedown2":
			takedown();
			break;
		case "oilrig":
		case "oilrig2":
			oilrig();
			break;
		case "dcemp":
		case "crashsite":
			dcemp();
			break;
		case "street":
			street();
			break;
		case "nrend":
		case "nrend2":
			nrend();
			break;
		case "knife":
		case "knife2":
			knife();
			break;
		case "body":
			body();
			break;
		case "cover":
			cover();
			break;
		case "loadout":
			loadout();
			break;
		case "boat":
		case "boat2":
			boat();
			break;
		case "lineoffire":
			lineoffire();
			break;
		case "bodyview":
			bodyview();
			break;
		case "look":
			look();
			break;
		case "targets":
			targets();
			break;
		case "pickups":
			pickups();
			break;
		case "buttons":
			buttons();
			break;
		case "bay":
			bay();
			break;
		case "introfreeze":
			introfreeze();
			break;
		case "course":
			course();
			break;
		case "betrayal":
			betrayal();
			break;
		case "tour":
			tour();
			break;
		case "cptour":
			cptour();
			break;
		case "rappel":
			rappel();
			break;
		case "chopperjump":
			chopperjump();
			break;
		case "scenewatch":
			scenewatch();
			break;
		case "gulagevac":
			gulagevac();
			break;
		case "oilbird":
			oilbird();
			break;
		case "invconvoy":
			invconvoy();
			break;
		case "bigjump":
			bigjump();
			break;
		case "climbview":
			if ( isdefined( level.mw2coop_qa_climbview ) )
				[[ level.mw2coop_qa_climbview ]]();
			else
				report( "climbview: not on Cliffhanger" );
			break;
		case "climb":
			// in maps\_mw2coop_climb_qa, loaded on Cliffhanger only (it names maps\_climb, which
			// other maps' zones do not have)
			if ( isdefined( level.mw2coop_qa_climb ) )
				[[ level.mw2coop_qa_climb ]]();
			else
				report( "climb: not on Cliffhanger" );
			break;
		case "aimassist":
			aimassist();
			break;
		case "bodycam":
			bodycam();
			break;
		case "glyphs":
			glyphs();
			break;
		case "armed":
			armed();
			break;
		default:
			report( "unknown scenario " + GetDvar( "mw2coop_qa" ) );
	}
}

report( text )
{
	level.mw2coop_qa_n++;
	SetDvar( "mw2coop_qa_log", level.mw2coop_qa_n + ": " + text );
	wait 0.3;	// the DLL polls every 250 ms
}

check( ok, text )
{
	if ( ok )
		report( "PASS " + text );
	else
		report( "FAIL " + text );
}

weapons_text( player )
{
	text = "";
	foreach ( weapon in player GetWeaponsListAll() )
		text += weapon + " ";
	return text;
}

common_checks()
{
	check( level.players.size == 2 && isdefined( level.player2 ), "two players (" + level.players.size + ")" );
	check( !is_specialop(), "campaign mode, not Spec Ops" );
	d = Distance( level.player.origin, level.player2.origin );
	check( d < 160, "player 2 next to player 1 (" + int( d ) + " units)" );
	report( "start point " + level.start_point + ", p1 weapons: " + weapons_text( level.player ) + "| p2 weapons: " + weapons_text( level.player2 ) );
}

// angles: (native split screen test) once a second: where each player is, where he looks (view angles) and which way the other one lies
// walk50: (native split screen test, "as on ice") player 2's server position and speed every 50 ms with the game time, to set against where the other context
// draws his body (native.c timelog) while a fake pad walks him forward and back
// p1front: (native split screen test) player 1 stands still 300 units in front of player 2 from 25 s on, for the whole run: does his body stay in player 2's half with time?
p1front()
{
	thread p1front_frames();
	level.mw2coop_no_follow = true;
	level.mw2coop_no_leash = true;
	wait 25;
	a = level.player;
	b = level.player2;
	yaw = b GetPlayerAngles()[ 1 ];
	a SetOrigin( b.origin + AnglesToForward( ( 0, yaw, 0 ) ) * 300 + ( 0, 0, 12 ) );
	a SetPlayerAngles( ( 0, yaw + 180, 0 ) );
	report( "p1front placed at t " + GetTime() + " p1 " + a.origin + " p2 " + b.origin );
}

p1front_frames()
{
	// a composed frame every 1 s from the start with a line in the log, to line the frames up with the native log's p0cnt/p0dump lines
	for ( i = 0; i < 70; i++ )
	{
		wait 1;
		SetDvar( "mw2coop_qa_frame", "pf-" + i );
		report( "pf " + i );
	}
}

// p1far: (native split screen test) player 2 stands, player 1 is put in front of him at growing distances, 4 s each (the mod's follow and leash are off): is player 1's body
// drawn in player 2's half at every distance? Composed frames come from [native] seqat / seqstep.
p1far()
{
	level.mw2coop_no_follow = true;
	level.mw2coop_no_leash = true;
	wait 30;
	a = level.player;
	b = level.player2;
	yaw = b GetPlayerAngles()[ 1 ];
	fwd = AnglesToForward( ( 0, yaw, 0 ) );
	origin = b.origin;
	dists = [];
	dists[ 0 ] = 150;
	dists[ 1 ] = 500;
	dists[ 2 ] = 900;
	dists[ 3 ] = 1400;
	dists[ 4 ] = 1900;
	dists[ 5 ] = 2400;
	foreach ( d in dists )
	{
		a SetOrigin( origin + fwd * d + ( 0, 0, 12 ) );
		a SetPlayerAngles( ( 0, yaw, 0 ) );
		report( "p1far distance " + d + " t " + GetTime() + " p1 " + a.origin + " p2 " + b.origin );
		wait 4;
	}
	report( "p1far done" );
}

walk50()
{
	for ( ;; )
	{
		wait 0.05;
		b = level.player2;
		report( "walk50 t " + GetTime() + " p2 " + b.origin + " v " + b GetVelocity() );
	}
}

angles_log()
{
	for ( ;; )
	{
		wait 0.2;
		a = level.player;
		b = level.player2;
		to_b = VectorToAngles( b.origin - a.origin );
		to_a = VectorToAngles( a.origin - b.origin );
		report( "angles t " + GetTime() + " p1 at " + a.origin + " looks " + a GetPlayerAngles() + " | p2 at " + b.origin + " looks " + b GetPlayerAngles() + " | p1 to p2 yaw " + to_b[ 1 ] + ", p2 to p1 yaw " + to_a[ 1 ] );
	}
}

// face: (native split screen test) after the briefing, player 2 stands 100 units in front of player 1 and the two look at each other
face()
{
	wait 90;
	a = level.player;
	b = level.player2;
	fwd = AnglesToForward( ( 0, a.angles[ 1 ], 0 ) );
	b SetOrigin( a.origin + fwd * 100 );
	a SetPlayerAngles( ( 0, a.angles[ 1 ], 0 ) );
	b SetPlayerAngles( ( 0, a.angles[ 1 ] + 180, 0 ) );
	report( "face: p1 " + a.origin + " p2 " + b.origin );
}

// gunface: (native split screen test) face(), and both players hold a rifle
gunface()
{
	wait 100;
	for ( i = 0; i < 600; i++ )
	{
		SetSavedDvar( "cg_drawGun", 1 );
		level.player GiveWeapon( "m4_grunt" );
		level.player2 GiveWeapon( "m4_grunt" );
		level.player EnableWeapons();
		level.player2 EnableWeapons();
		level.player SwitchToWeapon( "m4_grunt" );
		level.player2 SwitchToWeapon( "m4_grunt" );
		wait 0.5;
	}
}

// weapsw: (native split screen test) player 2's current weapon is logged every second: the pad's Y (fakepad.txt) must move it to the next of his list
weapsw()
{
	wait 100;
	level.player SwitchToWeapon( "usp_silencer" );
	for ( i = 0; i < 20; i++ )
	{
		report( "weapsw: p1 holds " + level.player GetCurrentWeapon() + ", p2 holds " + level.player2 GetCurrentWeapon() );
		wait 1;
	}
}

// hudtest: (native split screen test) a global hud element and one per player, to see which of them both halves draw
hudtest()
{
	wait 30;
	e = NewHudElem();
	e.x = 0;
	e.y = 60;
	e.alignX = "center";
	e.alignY = "middle";
	e.horzAlign = "center";
	e.vertAlign = "top";
	e.fontScale = 1.6;
	e.alpha = 1;
	e.sort = 100;
	e.foreground = true;
	e.hidewheninmenu = false;
	e.color = ( 1, 1, 0 );
	e SetText( "GLOBAL ELEM" );
	a = NewClientHudElem( level.player );
	a.x = -200;
	a.y = 100;
	a.alignX = "center";
	a.alignY = "middle";
	a.horzAlign = "center";
	a.vertAlign = "top";
	a.fontScale = 1.6;
	a.alpha = 1;
	a.sort = 100;
	a.foreground = true;
	a.hidewheninmenu = false;
	a.color = ( 1, 1, 0 );
	a SetText( "PLAYER ONE ELEM" );
	b = NewClientHudElem( level.player2 );
	b.x = 200;
	b.y = 100;
	b.alignX = "center";
	b.alignY = "middle";
	b.horzAlign = "center";
	b.vertAlign = "top";
	b.fontScale = 1.6;
	b.alpha = 1;
	b.sort = 100;
	b.foreground = true;
	b.hidewheninmenu = false;
	b.color = ( 1, 1, 0 );
	b SetText( "PLAYER TWO ELEM" );
	wait 600;
}

intro()
{
	common_checks();
	check( level.player2 GetWeaponsListAll().size == level.player GetWeaponsListAll().size, "same weapon count" );
	// a rifle player 1 gets stays his: each player takes his own off the range table (since
	// 2026-10-21; the real pickups are in the pickups scenario)
	level.player GiveWeapon( "m4_grunt" );
	level.player SwitchToWeapon( "m4_grunt" );
	wait 1;
	check( !level.player2 HasWeapon( "m4_grunt" ), "player 2 does not get player 1's rifle" );
	report( "done" );
}

restart()
{
	phase = GetDvar( "mw2coop_qa_phase" );
	if ( phase == "" )
	{
		common_checks();
		flag_set( "firing_range_hip_and_ads_done" );
		wait 1;
		check( GetDvar( "start" ) == "timed_ads", "checkpoint timed_ads reached (start dvar '" + GetDvar( "start" ) + "')" );
		SetDvar( "mw2coop_qa_phase", "after_restart" );
		report( "downing player 2" );
		level.player2 DoDamage( level.player2.health + 500, level.player2.origin );
		wait 1;
		check( level.player2 ent_flag( "coop_downed" ), "player 2 is down, not dead" );
		wait level.player_downed_death_buffer_time + 1;
		report( "downing player 1" );
		level.player DoDamage( level.player.health + 500, level.player.origin );
		wait 1;
		check( flag( "coop_alldowned" ), "both down -> all downed" );
		report( "waiting for the restart" );
		return;
	}
	if ( phase == "after_restart" )
	{
		SetDvar( "mw2coop_qa_phase", "" );
		check( level.start_point == "timed_ads", "restarted at checkpoint timed_ads (" + level.start_point + ")" );
		common_checks();
		check( !flag( "coop_alldowned" ), "players are up again" );
		report( "done" );
	}
}

ending()
{
	if ( GetDvar( "mw2coop_qa_phase" ) == "after_end" )
	{
		SetDvar( "mw2coop_qa_phase", "" );
		check( level.script == "roadkill", "next mission loaded (" + level.script + ")" );
		common_checks();
		report( "done" );
		return;
	}
	common_checks();
	SetDvar( "mw2coop_qa_phase", "after_end" );
	flag_set( "player_exiting_course_00" );
	wait 1;
	flag_set( "player_exiting_course" );
	wait 1;
	flag_set( "player_exiting_course_02" );
	report( "ending running" );
	wait 15;
	report( "FAIL still on " + level.script + " 15 s after the ending" );
}

// Continued session (qa_pair.sh resume @continue): both machines start at the saved checkpoint.
resume()
{
	common_checks();
	check( isdefined( level.start_point ) && level.start_point != "default", "level started at the saved checkpoint '" + level.start_point + "'" );
	report( "done" );
}

// God mode (on in QA: tools/qa_pair.sh sets mw2coop_god 1; see maps\_mw2coop::god_mode()):
// heavy damage downs nobody.
god()
{
	common_checks();
	check( maps\_mw2coop::god_mode_on(), "god mode on (mw2coop_god '" + GetDvar( "mw2coop_god" ) + "')" );
	wait 0.5;
	level.player2 DoDamage( level.player2.health + 500, level.player2.origin );
	level.player DoDamage( level.player.health + 500, level.player.origin );
	wait 1;
	check( IsAlive( level.player ) && IsAlive( level.player2 ), "both players alive after heavy damage" );
	check( !level.player ent_flag( "coop_downed" ) && !level.player2 ent_flag( "coop_downed" ), "neither player downed" );
	check( level.player.health > 0 && level.player2.health > 0, "health " + level.player.health + " / " + level.player2.health );
	report( "done" );
}

// Downs on Recruit (run with MW2COOP_GOD=0): the difficulty comes from the players' profiles,
// so the scenario forces Recruit as the game's own level.forcedgameskill does. Player 2 is
// downed, then player 1 after the grace time: both down fails the mission.
downs()
{
	common_checks();
	level.forcedgameskill = 0;
	maps\_gameskill::setSkill( true );
	wait 0.5;
	check( level.gameskill == 0, "difficulty Recruit (" + level.gameskill + ")" );
	check( isdefined( level.player_downed_death_buffer_time ), "time between downs defined on Recruit" );
	level.player2 DoDamage( level.player2.health + 500, level.player2.origin );
	wait 1;
	check( level.player2 ent_flag( "coop_downed" ) && !level.player ent_flag( "coop_downed" ), "player 2 downed, player 1 up" );
	wait 3;
	level.player DoDamage( level.player.health + 500, level.player.origin );
	wait 1;
	check( level.player ent_flag( "coop_downed" ), "player 1 downed after the grace time" );
	check( flag( "coop_alldowned" ) || level.mw2coop.failing, "both down: the mission fails" );
	report( "done" );
}

// A scene's values on the players survive a down and a revive (run with MW2COOP_GOD=0): levels
// set threatbias and attackeraccuracy for a scene (Loose Ends' accuracy ramp, Team Player's and
// The Only Easy Day's threatbias), and maps\_coop's resetSkill() on every down and revive put the
// difficulty's values back.
sceneskill()
{
	common_checks();
	foreach ( player in level.players )
	{
		player.threatbias = 777;
		player.attackeraccuracy = 0.123;
	}
	level.player2 DoDamage( level.player2.health + 500, level.player2.origin );
	wait 1.5;
	check( level.player2 ent_flag( "coop_downed" ), "player 2 downed" );
	check( level.player.threatbias == 777 && level.player.attackeraccuracy == 0.123, "player 1 keeps the scene's values after the down (" + level.player.threatbias + ", " + level.player.attackeraccuracy + ")" );
	level.player2 maps\_coop::player_coop_revive_self();
	wait 1.5;
	check( !level.player2 ent_flag( "coop_downed" ), "player 2 revived" );
	foreach ( n, player in level.players )
		check( player.threatbias == 777, "player " + ( n + 1 ) + " keeps the scene's threatbias after the revive (" + player.threatbias + ")" );
	check( level.player.attackeraccuracy == 0.123, "player 1 keeps the scene's attackeraccuracy after the revive (" + level.player.attackeraccuracy + ")" );
	report( "done" );
}

// The difficulty both machines play: each player's (player.gameskill, the host's pick in co-op)
// and the level's. mw2coop_qa_skill "<n>": the host's pick they must equal.
skill()
{
	common_checks();
	want = GetDvar( "mw2coop_qa_skill" );
	report( "skill: level " + level.gameskill + ", players " + level.player.gameskill + " " + level.player2.gameskill + ", profiles " + level.player GetPlayerSetting( "gameskill" ) + " " + level.player2 GetPlayerSetting( "gameskill" ) + ", mw2coop_skill '" + GetDvar( "mw2coop_skill" ) + "'" );
	if ( want != "" )
	{
		check( level.gameskill == int( want ), "level difficulty is the host's pick " + want + " (" + level.gameskill + ")" );
		foreach ( n, player in level.players )
			check( player.gameskill == int( want ), "player " + ( n + 1 ) + " plays difficulty " + want + " (" + player.gameskill + ")" );
	}
	report( "done" );
}

// Checkpoints from the level's sections (maps\_mw2coop::section_reached): the level passes into a
// new section, the mission fails, and both machines start again at that section. Run from a
// start point whose section ends by itself (Team Player "ride_later": the ride ends, "dismount"
// begins).
cprestart()
{
	want = "dismount";
	if ( GetDvar( "mw2coop_qa_phase" ) == "cp_failed" )
	{
		SetDvar( "mw2coop_qa_phase", "" );
		check( level.start_point == want, "after the failed mission the level starts at the checkpoint " + want + " (" + level.start_point + ")" );
		common_checks();
		report( "done" );
		return;
	}
	common_checks();
	for ( t = 0; t < 90 && GetDvar( "start" ) != want; t += 0.5 )
		wait 0.5;
	check( GetDvar( "start" ) == want, "checkpoint " + want + " reached (start '" + GetDvar( "start" ) + "')" );
	wait 2;
	SetDvar( "mw2coop_qa_phase", "cp_failed" );
	report( "failing the mission" );
	level thread maps\_mw2coop::mission_failed();
	wait 30;
	report( "FAIL no restart 30 s after the failure" );
}

// Friendly fire between players (run with MW2COOP_GOD=0; the lobby setting through
// MW2COOP_HOST_CMDS="set mw2coop_ff 0"): player 2 stands 80 units in front of player 1, who aims
// at his chest and fires for real; player 2's health before and after.
pvp()
{
	common_checks();
	wait 14;	// after the mission title
	foreach ( player in level.players )
	{
		player EnableWeapons();
		player FreezeControls( false );
	}
	fwd = AnglesToForward( ( 0, level.player GetPlayerAngles()[ 1 ], 0 ) );
	level.player2 SetOrigin( level.player.origin + fwd * 80 );
	wait 0.5;
	aim = VectorToAngles( ( level.player2.origin + ( 0, 0, 48 ) ) - level.player GetEye() );
	level.player SetPlayerAngles( aim );
	wait 0.5;
	before = level.player2.health;
	report( "player 1 weapon " + level.player GetCurrentWeapon() + ", g_friendlyfireDamageScale " + GetDvar( "g_friendlyfireDamageScale" ) + ", player 2 health " + before );
	press( level.player, "attack", 600 );
	wait 1.5;
	report( "player 2 health after player 1 fired: " + level.player2.health + " (was " + before + "), downed " + level.player2 ent_flag( "coop_downed" ) );
	report( "done" );
}

// The checkpoint table of the level (maps\_mw2coop_checkpoints): every flag in it exists.
cpcheck()
{
	common_checks();
	wait 5;
	pairs = maps\_mw2coop_checkpoints::table( level.script );
	report( level.script + ": " + pairs.size + " checkpoint flags" );
	foreach ( p in pairs )
		check( isdefined( level.flag[ p[ 0 ] ] ), "flag " + p[ 0 ] + " (" + p[ 1 ] + ") exists" );
	report( "done" );
}

// The Predator's UAV view for one player (maps\_remotemissile as patched: SetSavedDvar cg_fov 26
// plus maps\_mw2coop::fov_pilot): the pilot's screen zooms, the partner's keeps its field of
// view (src/fov.c). Player 1 is the pilot, then player 2. Frames on both machines; the DLL logs
// cg_fov (MW2COOP_QA_WATCH=cg_fov) and "fov: ... keeps".
pilotfov()
{
	common_checks();
	wait 14;	// after the mission title
	foreach ( n, pilot in level.players )
	{
		SetSavedDvar( "cg_fov", 26 );
		maps\_mw2coop::fov_pilot( pilot );
		report( "player " + ( n + 1 ) + " is the pilot (cg_fov 26)" );
		wait 1.5;
		qa_shot( "pilot" + ( n + 1 ) );
		SetSavedDvar( "cg_fov", 65 );
		maps\_mw2coop::fov_pilot( undefined );
		wait 1.5;
	}
	qa_shot( "after" );
	report( "done" );
}

// Team Player's ride (start point "ride", maps\_mw2coop_roadkill): the passenger sees "Hold [Use]
// to take the turret"; he holds Use for real, the two swap, and the new passenger sees it.
swaphint()
{
	common_checks();
	for ( t = 0; t < 30 && !( isdefined( level.mw2coop_roadkill_seat ) && isdefined( level.mw2coop_roadkill_seat.mw2coop_player ) ); t += 0.5 )
		wait 0.5;
	seat = level.mw2coop_roadkill_seat;
	check( isdefined( seat ) && isdefined( seat.mw2coop_player ), "a passenger is seated" );
	if ( !isdefined( seat ) || !isdefined( seat.mw2coop_player ) )
	{
		report( "done" );
		return;
	}
	wait 1;
	passenger = seat.mw2coop_player;
	gunner = maps\_mw2coop::vehicle_gunner();
	check( isdefined( passenger.mw2coop_swap_hint_elem ), player_name( passenger ) + " (passenger) sees the swap hint" );
	check( !isdefined( gunner ) || !isdefined( gunner.mw2coop_swap_hint_elem ), "the gunner does not" );
	qa_shot( "a-hint" );
	press( passenger, "activate", 1500 );
	wait 3;
	check( isdefined( level.player_turret ) && level.player_turret GetTurretOwner() == passenger, player_name( passenger ) + " took the turret" );
	now = seat.mw2coop_player;
	check( isdefined( now ) && now != passenger, "the other player is the passenger now" );
	if ( isdefined( now ) )
		check( isdefined( now.mw2coop_swap_hint_elem ), player_name( now ) + " (new passenger) sees the swap hint" );
	qa_shot( "b-swapped" );
	wait 7;
	check( !isdefined( passenger.mw2coop_swap_hint_elem ) && ( !isdefined( now ) || !isdefined( now.mw2coop_swap_hint_elem ) ), "the hint is gone after a few seconds" );
	report( "done" );
}

// Whiskey Hotel's roof (start point "flare", maps\_mw2coop_whitehouse): on "Use your flares!"
// each player holds a flare of his own; player 2 pops his with a real Attack press and player 1
// keeps his; then player 1 pops his.
flares()
{
	common_checks();
	// the way the level goes: the breach, then the roof (the bots do not walk there)
	foreach ( name in strtok( "whitehouse_flare_breach whitehouse_flare_run whitehouse_path_roof", " " ) )
	{
		trigger = getEntWithFlag( name );
		if ( !isdefined( trigger ) || flag( name ) )
			continue;
		level.player SetOrigin( trigger.origin );
		level.player2 SetOrigin( trigger.origin + ( 40, 0, 0 ) );
		report( "both moved to the trigger of " + name );
		for ( t = 0; !flag( name ) && t < 6; t += 0.5 )
			wait 0.5;
	}
	for ( t = 0; !flag( "player_flare" ) && t < 40; t += 0.5 )
		wait 0.5;
	if ( !flag( "player_flare" ) )
	{
		// Foley's flare scene did not reach the roof with the bots: start the jets as it would
		report( "test shortcut: the jets flag set by the test (Foley's scene did not get there)" );
		flag_set( "whitehouse_flare_run" );
		flag_set( "whitehouse_hammerdown_jets_fly" );
		for ( t = 0; !flag( "player_flare" ) && t < 10; t += 0.5 )
			wait 0.5;
	}
	check( flag( "player_flare" ), "Foley calls for the flares" );
	wait 1.5;
	foreach ( n, player in level.players )
		check( player HasWeapon( "flare" ) && player GetCurrentWeapon() == "flare", "player " + ( n + 1 ) + " holds a flare (" + player GetCurrentWeapon() + ")" );
	qa_shot( "a-flares" );
	press( level.player2, "attack", 300 );
	wait 12;	// lighting and waving the flare, then it is taken
	qa_shot( "b-player2-popped" );
	check( !level.player2 HasWeapon( "flare" ), "player 2's flare is used up" );
	check( level.player HasWeapon( "flare" ), "player 1 still has his flare" );
	check( flag( "player_flare_popped" ), "the flare hint is done" );
	press( level.player, "attack", 300 );
	wait 12;
	qa_shot( "c-player1-popped" );
	check( !level.player HasWeapon( "flare" ), "player 1's flare is used up" );
	foreach ( n, player in level.players )
		check( player GetCurrentWeapon() != "flare" && player GetCurrentWeapon() != "none", "player " + ( n + 1 ) + " back on a weapon (" + player GetCurrentWeapon() + ")" );
	report( "done" );
}

// Passenger ride (Wolverines! opening: start point default on invasion): player 2 rides on a
// free seat of player 1's vehicle and gets out with him.
ride()
{
	check( level.players.size == 2 && isdefined( level.player2 ), "two players (" + level.players.size + ")" );
	t = 0;
	while ( !level.player IsLinked() && t < 30 )
	{
		wait 0.5;
		t += 0.5;
	}
	check( level.player IsLinked(), "player 1 seated" );
	wait 1.5;
	check( level.player2 IsLinked(), "player 2 seated too" );
	check( Distance( level.player.origin, level.player2.origin ) < 120, "player 2 in the same vehicle (" + int( Distance( level.player.origin, level.player2.origin ) ) + " units)" );
	wait 6;
	check( Distance( level.player.origin, level.player2.origin ) < 120, "still together while driving (" + int( Distance( level.player.origin, level.player2.origin ) ) + " units)" );
	t = 0;
	while ( level.player IsLinked() && t < 120 )
	{
		wait 0.5;
		t += 0.5;
	}
	check( !level.player IsLinked(), "player 1 got out" );
	wait 1.5;
	check( !level.player2 IsLinked(), "player 2 got out" );
	check( Distance( level.player.origin, level.player2.origin ) < 160, "player 2 next to player 1 (" + int( Distance( level.player.origin, level.player2.origin ) ) + " units)" );
	report( "done" );
}

// Turret hand-over experiment (roadkill, start point "ride": player 1 starts on the Humvee
// turret). Takes player 1 off with a second UseBy and puts player 2 on.
// Team Player's passenger seat: what player 2 sees from the rear seat while player 1 is the gunner
// (frames of both machines in mw2coop\shots; the client's is player 2's).
seatview()
{
	check( level.players.size == 2 && isdefined( level.player2 ), "two players (" + level.players.size + ")" );
	t = 0;
	while ( !( isdefined( level.player_turret ) && isdefined( level.player_turret GetTurretOwner() ) ) && t < 30 )
	{
		wait 0.5;
		t += 0.5;
	}
	wait 6;
	report( "gunner " + ( level.player_turret GetTurretOwner() == level.player ) + ", player 2 linked " + level.player2 IsLinked() + " at " + level.player2.origin + " angles " + level.player2 GetPlayerAngles() + ", humvee " + level.player_humvee.origin );
	yaw = AngleClamp180( level.player2 GetPlayerAngles()[ 1 ] - level.player_humvee.angles[ 1 ] );
	check( Abs( yaw ) < 40, "player 2 starts looking ahead (" + int( yaw ) + " degrees off the Humvee's heading)" );
	hp = level.player.health;
	level.player DoDamage( 30, level.player.origin, level.player2, level.player2, "MOD_RIFLE_BULLET" );
	wait 0.5;
	check( level.player.health >= hp, "player 2's bullets do not hurt the gunner (" + hp + " -> " + level.player.health + ")" );
	qa_shot( "seat-front" );
	level.player2 SetPlayerAngles( level.player2 GetPlayerAngles() + ( 0, 90, 0 ) );
	wait 0.5;
	qa_shot( "seat-side" );
	level.player2 SetPlayerAngles( level.player2 GetPlayerAngles() + ( 0, 90, 0 ) );
	wait 0.5;
	qa_shot( "seat-back" );
	report( "done" );
}

turret()
{
	check( level.players.size == 2 && isdefined( level.player2 ), "two players (" + level.players.size + ")" );
	t = 0;
	while ( !isdefined( level.player_turret ) && t < 20 )
	{
		wait 0.5;
		t += 0.5;
	}
	turret = level.player_turret;
	check( isdefined( turret ), "player turret exists" );
	wait 2;
	check( turret GetTurretOwner() == level.player, "player 1 is the gunner" );

	level.player EnableTurretDismount();
	turret UseBy( level.player );
	wait 0.5;
	owner = turret GetTurretOwner();
	check( !isdefined( owner ), "second UseBy takes player 1 off the turret" );
	report( "player 1 using a turret: " + level.player IsUsingTurret() + ", linked: " + level.player IsLinked() );

	level.player2 PlayerLinkToDelta( level.player_humvee, "tag_player", 0.35, 360, 360, 45, 30, true );
	turret MakeUsable();
	turret UseBy( level.player2 );
	turret MakeUnusable();
	level.player2 DisableTurretDismount();
	wait 0.5;
	check( turret GetTurretOwner() == level.player2, "player 2 is the gunner now" );
	report( "player 2 using a turret: " + level.player2 IsUsingTurret() + ", linked: " + level.player2 IsLinked() );
	wait 3;
	check( turret GetTurretOwner() == level.player2, "player 2 still on the turret after 3 s" );
	report( "done" );
}

// Team Player boarding (roadkill, start point "convoy"): player 2 reaches the seat first and
// becomes the gunner, player 1 boards the rear seat, the convoy leaves, the seats swap and swap
// back (called directly: a script cannot hold the Use button), both stay in the moving Humvee.
board()
{
	check( level.players.size == 2 && isdefined( level.player2 ), "two players (" + level.players.size + ")" );
	t = 0;
	while ( !isdefined( level.chair ) && t < 60 )
	{
		wait 0.25;
		t += 0.25;
	}
	check( isdefined( level.chair ), "glowing seat spawned" );
	flag_set( "player_climbs_stairs" );	// a trigger player 1 would touch on the stairs: the convoy moment
	// player 2 waits at the seat until someone boards
	t = 0;
	while ( !isdefined( level.mw2coop_gunner ) && t < 90 )
	{
		if ( isdefined( level.chair ) )
			level.player2 SetOrigin( level.chair.origin + ( 0, 0, -10 ) );
		wait 0.1;
		t += 0.1;
	}
	check( isdefined( level.mw2coop_gunner ) && level.mw2coop_gunner == level.player2, "player 2 boarded first and is the gunner" );
	t = 0;
	while ( !( isdefined( level.player_turret ) && isdefined( level.player_turret GetTurretOwner() ) ) && t < 15 )
	{
		wait 0.25;
		t += 0.25;
	}
	check( isdefined( level.player_turret ) && level.player_turret GetTurretOwner() == level.player2, "player 2 on the turret" );
	check( !flag( "player_gets_in" ), "convoy waits for player 1" );
	wait 1;
	level.player SetOrigin( level.player_humvee GetTagOrigin( "tag_seat_rb_attach" ) + ( 0, 0, -10 ) );
	wait 1;
	check( level.player IsLinked(), "player 1 boarded the rear seat" );
	check( flag( "player_gets_in" ), "convoy leaves once both are in" );
	wait 2;

	level.player_humvee maps\_mw2coop_roadkill::swap( level.mw2coop_roadkill_seat );
	wait 1;
	check( level.player_turret GetTurretOwner() == level.player && level.player2 IsLinked(), "swap: player 1 gunner, player 2 seated" );
	level.player_humvee maps\_mw2coop_roadkill::swap( level.mw2coop_roadkill_seat );
	wait 1;
	check( level.player_turret GetTurretOwner() == level.player2 && level.player IsLinked(), "swap back: player 2 gunner, player 1 seated" );

	wait 6;
	d = Distance( level.player.origin, level.player2.origin );
	check( d < 150 && level.player IsLinked() && level.player2 IsLinked(), "both riding the moving Humvee (" + int( d ) + " units apart)" );
	report( "done" );
}

// Team Player ride end (roadkill, start point "ride_later": player 1 starts on the turret):
// player 2 rides on a seat, both get off when the ride ends, player 2 lands next to player 1.
rideend()
{
	check( level.players.size == 2 && isdefined( level.player2 ), "two players (" + level.players.size + ")" );
	wait 3;
	check( isdefined( level.player_turret ) && level.player_turret GetTurretOwner() == level.player, "player 1 on the turret" );
	check( Distance( level.player.origin, level.player_turret.origin ) < 150, "player 1 at the turret (" + int( Distance( level.player.origin, level.player_turret.origin ) ) + " units)" );
	check( level.player2 IsLinked(), "player 2 seated" );
	report( "t 3: player 1 " + int( Distance( level.player.origin, level.player_turret.origin ) ) + " units from the turret, player 2 " + int( Distance( level.player2.origin, level.player_turret.origin ) ) );
	t = 0;
	while ( !flag( "player_is_dismounted" ) && t < 180 )
	{
		wait 0.5;
		t += 0.5;
		if ( t == int( t / 2 ) * 2 && t <= 12 || t == int( t / 10 ) * 10 )
			report( "t " + t + ": turret " + level.player IsUsingTurret() + ", " + int( Distance( level.player.origin, level.player2.origin ) ) + " units, linked " + level.player IsLinked() + "/" + level.player2 IsLinked()
				+ ", flags " + flag_text( "lead_vehicle_speeds_up" ) + flag_text( "resume_the_path" ) + flag_text( "player_is_pushing_truck" ) + flag_text( "player_goes_in_reverse" ) + flag_text( "player_vehicle_wipes_out" ) );
	}
	check( flag( "player_is_dismounted" ), "ride ended in the crash (" + t + " s)" );
	wait 1;
	check( !level.player IsLinked() && !level.player2 IsLinked(), "both off the Humvee" );
	d = Distance( level.player.origin, level.player2.origin );
	check( d < 200, "player 2 next to player 1 after the crash (" + int( d ) + " units)" );
	report( "done" );
}

flag_text( name )
{
	if ( !isdefined( level.flag[ name ] ) )
		return "?";
	if ( flag( name ) )
		return "1";
	return "0";
}

// QA probe, started from maps\_mw2coop::main(): when player 1's link changes in the first 3 s.
link_probe()
{
	text = "";
	last = -1;
	for ( i = 0; i < 60; i++ )
	{
		now = 0;
		if ( level.player IsLinked() )
			now = 1;
		if ( now != last )
			text += " frame " + i + ": " + now + " @" + int( level.player.origin[ 0 ] ) + "," + int( level.player.origin[ 1 ] );
		last = now;
		wait 0.05;
	}
	maps\_mw2coop::state( "link probe:" + text );
}

// Team Player opening (roadkill, default start): during Shepherd's "get on the line" scene both
// players are on the view rig; afterwards both are free and together.
rkintro()
{
	check( level.players.size == 2 && isdefined( level.player2 ), "two players (" + level.players.size + ")" );
	t = 0;
	both = false;
	while ( !flag( "get_on_the_line" ) && t < 90 )
	{
		if ( level.player IsLinked() && level.player2 IsLinked() )
			both = true;
		wait 0.25;
		t += 0.25;
	}
	check( flag( "get_on_the_line" ), "Shepherd's scene ended (" + t + " s)" );
	check( both, "both players were in the scene" );
	wait 1;
	check( !level.player IsLinked() && !level.player2 IsLinked(), "both free after the scene" );
	d = Distance( level.player.origin, level.player2.origin );
	check( d < 200, "player 2 next to player 1 (" + int( d ) + " units)" );
	report( "done" );
}

// Leash (S.S.D.D.): player 2 is put 4000 units away; within leash_time() + 2 s he is back
// next to player 1.
leash()
{
	common_checks();
	level.player2 SetOrigin( level.player.origin + ( 4000, 0, 0 ) );
	wait 1;
	check( Distance( level.player.origin, level.player2.origin ) > 3000, "player 2 is far away (" + int( Distance( level.player.origin, level.player2.origin ) ) + " units)" );
	wait maps\_mw2coop::leash_time() + 1;
	d = Distance( level.player.origin, level.player2.origin );
	check( d < 200, "leash brought player 2 back (" + int( d ) + " units)" );
	report( "done" );
}

// Cliffhanger snowmobiles (start point "snowmobile"): each player on his own snowmobile; at the
// helicopter the ending waits while player 2 is still on his snowmobile and goes on once he is
// off next to player 1. (A script cannot drive, so the ride itself is skipped by setting the
// level's flags.)
snow()
{
	check( level.players.size == 2 && isdefined( level.player2 ), "two players (" + level.players.size + ")" );
	wait 3;
	check( isdefined( level.player.vehicle ), "player 1 on a snowmobile" );
	check( isdefined( level.player2.vehicle ) && ( !isdefined( level.player.vehicle ) || level.player2.vehicle != level.player.vehicle ), "player 2 on his own snowmobile" );
	foreach ( name in strtok( "player_reaches_hilltop there_is_chopper ending_heli_flies_in", " " ) )
	{
		qa_flag_set( name );
		wait 1;
	}
	level.player player_dismount_vehicle();
	wait 8;	// the radio lines between the flags
	SetDvar( "mw2coop_qa_phase", "done" );
	qa_flag_set( "end_begins" );
	wait 5;
	check( !level.mw2coop.completing, "ending waits while player 2 is on his snowmobile" );
	level.player2 player_dismount_vehicle();
	wait 1;
	level.player2 maps\_mw2coop::teleport_to_partner( level.player );
	t = 0;
	while ( !level.mw2coop.completing && t < 15 )
	{
		wait 0.25;
		t += 0.25;
	}
	check( level.mw2coop.completing, "mission ends once player 2 is there (" + t + " s)" );
	report( "done" );
}

qa_flag_set( name )
{
	if ( !isdefined( level.flag[ name ] ) )
		flag_init( name );
	flag_set( name );
}

// self: a snowmobile ridden in first person: where its attached hands hold the bars
report_grips()
{
	seat = self GetTagOrigin( "tag_driver" );
	fwd = AnglesToForward( self.angles );
	right = AnglesToRight( self.angles );
	text = "grips:";
	foreach ( tag in strtok( "j_wrist_ri j_wrist_le", " " ) )
	{
		d = self GetTagOrigin( tag ) - seat;
		text += " " + tag + " " + int( VectorDot( d, fwd ) ) + "/" + int( VectorDot( d, right ) ) + "/" + int( d[ 2 ] );
	}
	report( text );
}

// "head f/r/u ..." of a rider body: its bones from the vehicle's tag_driver, along the vehicle
rider_bones( r, v )
{
	seat = v GetTagOrigin( "tag_driver" );
	fwd = AnglesToForward( v.angles );
	right = AnglesToRight( v.angles );
	text = "";
	foreach ( tag in strtok( "j_head j_spine4 j_hip_le j_wrist_ri j_wrist_le j_knee_le j_ankle_le", " " ) )
	{
		d = r GetTagOrigin( tag ) - seat;
		text += tag + " " + int( VectorDot( d, fwd ) ) + "/" + int( VectorDot( d, right ) ) + "/" + int( d[ 2 ] ) + "  ";
	}
	return text;
}

// The Gulag's cellblock rope (start "rappel"): player 1 uses it and rappels, the rope hangs ready
// again, player 2 uses it and rappels; both end up below, not linked, with their weapons.
rappel()
{
	check( level.players.size == 2 && isdefined( level.player2 ), "two players (" + level.players.size + ")" );
	if ( level.script == "af_caves" )
	{
		cave_rappel();
		return;
	}
	wait 4;
	trigger = GetEnt( "rappel_trigger", "script_noteworthy" );
	check( isdefined( trigger ), "the rope's use trigger exists" );
	top = level.player.origin;
	level.player2 SetOrigin( level.player.origin + ( 30, 0, 0 ) );
	wait 1;
	trigger notify( "trigger", level.player );
	wait 3;
	check( level.player IsLinked(), "player 1 is on the rope" );
	level.player2 SetOrigin( level.player.origin + ( 30, 0, 0 ) );
	wait 12;
	d1 = top[ 2 ] - level.player.origin[ 2 ];
	check( !level.player IsLinked() && d1 > 100, "player 1 came down and was let go (" + int( d1 ) + " units)" );
	check( isdefined( GetEnt( "rappel_trigger", "script_noteworthy" ) ), "the rope is usable again for player 2" );
	level.player2 SetOrigin( top + ( 20, 0, 0 ) );
	wait 1;
	trigger notify( "trigger", level.player2 );
	wait 3;
	check( level.player2 IsLinked(), "player 2 is on the rope" );
	wait 12;
	d2 = top[ 2 ] - level.player2.origin[ 2 ];
	check( !level.player2 IsLinked() && d2 > 100, "player 2 came down and was let go (" + int( d2 ) + " units)" );
	d = Distance( level.player.origin, level.player2.origin );
	report( "at the bottom: " + int( d ) + " units apart" );
	report( "player 1 holds " + level.player GetCurrentWeapon() + ", player 2 holds " + level.player2 GetCurrentWeapon() );
	report( "done" );
}

// Just Like Old Times (start "rappel"): player 1 hooks up, rappels and kills the guard; the hook-up is
// free again, player 2 hooks up and rappels; both end up below, not linked, with their weapons.
cave_rappel()
{
	t = 0;
	while ( !flag( "price_hooksup" ) && t < 60 )
	{
		wait 0.5;
		t += 0.5;
	}
	check( flag( "price_hooksup" ), "Price hooked up" );
	wait 2;
	trigger = GetEnt( "player_rappel_trigger", "targetname" );
	check( isdefined( trigger ), "the hook-up's use trigger exists" );
	top = level.player.origin;
	level.player2 SetOrigin( top + ( 40, 0, 0 ) );
	wait 1;
	trigger notify( "trigger", level.player );
	wait 3;
	check( level.player IsLinked(), "player 1 is on the rope" );
	t = 0;
	while ( !flag( "end_of_rappel_scene" ) && t < 90 )
	{
		if ( flag( "rappel_end" ) && !flag( "player_killing_guard" ) )
			press( level.player, "melee", 500 );
		wait 0.5;
		t += 0.5;
	}
	check( flag( "player_killing_guard" ), "player 1 killed the guard" );
	check( flag( "end_of_rappel_scene" ), "player 1's scene ended (" + t + " s)" );
	wait 6;
	d1 = top[ 2 ] - level.player.origin[ 2 ];
	check( !level.player IsLinked() && d1 > 100, "player 1 came down and was let go (" + int( d1 ) + " units)" );
	check( isdefined( trigger ), "the hook-up is usable again for player 2" );
	// the barracks' "abandoned Price" check fails the mission for a player 1 who stays at the bottom
	if ( isdefined( level.flag[ "barracks_stealthbreak_survived" ] ) )
		flag_set( "barracks_stealthbreak_survived" );
	bottom1 = level.player.origin;
	level.player2 SetOrigin( top + ( 40, 0, 0 ) );
	wait 1;
	trigger notify( "trigger", level.player2 );
	wait 3;
	check( level.player2 IsLinked(), "player 2 is on the rope" );
	t = 0;
	while ( level.player2 IsLinked() && t < 90 )
	{
		wait 0.5;
		t += 0.5;
	}
	wait 2;
	d2 = top[ 2 ] - level.player2.origin[ 2 ];
	check( !level.player2 IsLinked() && d2 > 100, "player 2 came down and was let go (" + int( d2 ) + " units, " + t + " s)" );
	check( !level.player IsLinked() && Distance( level.player.origin, bottom1 ) < 200, "player 1 stayed at the bottom while player 2 rappelled (" + int( Distance( level.player.origin, bottom1 ) ) + " units)" );
	report( "at the bottom: " + int( Distance( level.player.origin, level.player2.origin ) ) + " units apart" );
	report( "player 1 holds " + level.player GetCurrentWeapon() + ", player 2 holds " + level.player2 GetCurrentWeapon() );
	report( "done" );
}

// Escape from Favela (start "chopperjump"): player 2 leaps for the chopper's rope ladder (real keys:
// forward, then jump, facing the ladder); player 1 catches the rope too; both are on it when the
// chopper flies off.
chopperjump()
{
	check( level.players.size == 2 && isdefined( level.player2 ), "two players (" + level.players.size + ")" );
	wait 8;
	vol = GetEnt( "trig_player_chopperjump", "script_noteworthy" );
	check( isdefined( vol ), "the jump volume exists" );
	t = 0;
	while ( !isdefined( level.chopperladder ) && t < 30 )
	{
		wait 0.5;
		t += 0.5;
	}
	check( isdefined( level.chopperladder ), "the ladder exists (" + t + " s)" );
	if ( !isdefined( vol ) || !isdefined( level.chopperladder ) )
		return;
	report( "volume " + vol.origin + ", ladder " + level.chopperladder.origin + ", player 1 at " + level.player.origin + ", player 2 at " + level.player2.origin );
	// a run-up of 130 units to the ledge, facing the ladder, then jumps while running (the first
	// attempt pressed jump 0.7 s after forward: he had walked off the ledge by then)
	look = level.chopperladder.origin + ( 0, 0, -85 );
	toward = VectorNormalize( ( look[ 0 ] - vol.origin[ 0 ], look[ 1 ] - vol.origin[ 1 ], 0 ) );
	start = vol.origin - toward * 130;
	spot = PlayerPhysicsTrace( start + ( 0, 0, 40 ), start - ( 0, 0, 400 ) );
	level.player2 SetOrigin( spot );
	wait 0.5;
	level.player2 SetPlayerAngles( VectorToAngles( look - level.player2 GetEye() ) );
	wait 0.5;
	report( "player 2 starts at " + level.player2.origin + " and the volume is touched: " + level.player2 IsTouching( vol ) );
	press( level.player2, "forward", 3000 );
	wait 0.5;
	for ( n = 0; n < 12 && !flag( "chopperjump_player_jump" ); n++ )
	{
		press( level.player2, "gostand", 150 );
		wait 0.1;
		report( "run " + n + ": player 2 at " + level.player2.origin + " speed " + int( Length( level.player2 GetVelocity() ) ) + " touches " + level.player2 IsTouching( vol ) );
	}
	wait 3;
	check( flag( "chopperjump_player_jump" ), "the jump was caught" );
	// frames of both screens (host = player 1, client = player 2): on the rope, climbing, flying away
	qa_shot( "rope-a" );
	report( "rope: player 1 at " + vec_int( level.player.origin ) + ", player 2 at " + vec_int( level.player2.origin ) + ", " + int( Distance( level.player.origin, level.player2.origin ) ) + " apart" );
	qa_outside( "rope-outside" );
	wait 1.0;
	qa_shot( "rope-b" );
	wait 2.5;
	qa_shot( "rope-c" );
	check( level.player2 IsLinked(), "player 2 is on the rope" );
	check( level.player IsLinked(), "player 1 caught the rope too" );
	t = 0;
	while ( !flag( "solorun_player_boarded_chopper" ) && t < 20 )
	{
		wait 0.5;
		t += 0.5;
	}
	check( flag( "solorun_player_boarded_chopper" ), "the chopper took them" );
	wait 10;
	check( level.player2 IsLinked() && level.player IsLinked(), "both still on the rope while the chopper flies" );
	report( "done" );
}

// Any scripted scene: nobody touches anything. Every 5 s both screens are saved (host = player 1,
// client = player 2) and each player's state is logged: linked, stance, position, weapon.
scenewatch()
{
	check( level.players.size == 2 && isdefined( level.player2 ), "two players (" + level.players.size + ")" );
	for ( n = 0; n < 9; n++ )
	{
		wait 4.4;
		qa_shot( "scene" + n );
		foreach ( player in level.players )
			report( "t" + n + " " + player_name( player ) + ": at " + vec_int( player.origin ) + ", linked " + player IsLinked() + ", " + player GetStance() + ", weapon " + player GetCurrentWeapon() + ", alive " + isalive( player ) );
	}
	report( "done" );
}

// The Gulag's evacuation (start "evac"): both players lie buried, get free, clip on the Pavelow's rope
// by their own use and are pulled away together. Frames of both screens at each step.
gulagevac()
{
	check( level.players.size == 2 && isdefined( level.player2 ), "two players (" + level.players.size + ")" );
	wait 4;
	qa_shot( "buried" );
	check( level.player IsLinked() && level.player2 IsLinked(), "both are buried in the scene (" + level.player IsLinked() + " " + level.player2 IsLinked() + ")" );
	t = 0;
	while ( level.player IsLinked() && t < 40 )
	{
		wait 0.5;
		t += 0.5;
	}
	wait 1;
	check( !level.player IsLinked() && !level.player2 IsLinked(), "both are free (" + t + " s)" );
	trigger = getEntWithFlag( "player_uses_rig" );
	check( isdefined( trigger ), "the rope's trigger exists" );
	qa_shot( "free" );
	trigger notify( "trigger", level.player );
	wait 1.5;
	check( level.player IsLinked() && !level.player2 IsLinked(), "player 1 clipped on, player 2 not yet" );
	check( !flag( "player_gets_pulled" ), "nobody is pulled away yet" );
	trigger notify( "trigger", level.player2 );
	wait 1.5;
	check( level.player2 IsLinked(), "player 2 clipped on too" );
	qa_shot( "clipped" );
	report( "evac: player 1 at " + vec_int( level.player.origin ) + ", player 2 at " + vec_int( level.player2.origin ) + ", " + int( Distance( level.player.origin, level.player2.origin ) ) + " apart" );
	qa_outside( "outside" );
	t = 0;
	while ( !flag( "player_gets_pulled" ) && t < 40 )
	{
		wait 0.5;
		t += 0.5;
	}
	check( flag( "player_gets_pulled" ), "Soap pulls (" + t + " s)" );
	wait 1;
	qa_shot( "pulled" );
	wait 3;
	qa_shot( "flying" );
	check( level.player IsLinked() && level.player2 IsLinked(), "both are on the rope while it flies" );
	report( "done" );
}

nearest_seat( bird, org )
{
	best = "none";
	d = 100000;
	for ( n = 1; n <= 4; n++ )
	{
		dd = Distance( org, bird GetTagOrigin( "tag_guy" + n ) );
		if ( dd < d )
		{
			d = dd;
			best = "tag_guy" + n + " (" + int( dd ) + ")";
		}
	}
	return best;
}

// Oil Rig's escape (start "escape"): the little bird that takes the players off the roof. Sets the
// flags the walk would, waits for the landing, and logs the seats (tags) and the riders.
oilbird()
{
	check( level.players.size == 2 && isdefined( level.player2 ), "two players (" + level.players.size + ")" );
	wait 3;
	t = 0;
	while ( !flag( "escape_littlebird_landed" ) && t < 120 )
	{
		// the walk's flags, in order, as the level reaches the waits for them
		foreach ( name in strtok( "barracks_cleared player_headed_out_of_barracks player_approaching_derrick_building_exit player_approaching_helipad", " " ) )
			if ( isdefined( level.flag[ name ] ) && !flag( name ) && ( name != "player_headed_out_of_barracks" || flag( "littlebird_escape_spawned" ) ) )
				flag_set( name );
		wait 1;
		t += 1;
	}
	check( flag( "escape_littlebird_landed" ), "the little bird landed (" + t + " s)" );
	wait 4;
	bird = level.littlebird_escape;
	check( isdefined( bird ), "the bird exists" );
	if ( !isdefined( bird ) )
		return;
	report( "bird at " + vec_int( bird.origin ) + " angles " + bird.angles );
	for ( n = 1; n <= 4; n++ )
		report( "tag_guy" + n + " at " + vec_int( bird GetTagOrigin( "tag_guy" + n ) ) );
	t = 0;
	while ( ( !isdefined( bird.riders ) || bird.riders.size < 8 ) && t < 40 )
	{
		wait 0.5;
		t += 0.5;
	}
	report( "riders loaded: " + bird.riders.size + " (" + t + " s)" );
	foreach ( ai in GetAIArray( "allies" ) )
		if ( isdefined( ai.script_startingposition ) )
			report( "ally pos " + ai.script_startingposition + " at " + vec_int( ai.origin ) + " nearest seat " + nearest_seat( bird, ai.origin ) + " name " + ai.classname );
	report( "players " + vec_int( level.player.origin ) + " " + vec_int( level.player2.origin ) );
	qa_shot( "bird" );
	// both walk up to the seat trigger: the bird takes them
	spot = bird GetTagOrigin( "tag_guy3" ) + ( 0, 0, 10 );
	level.player SetOrigin( spot );
	wait 0.5;
	level.player2 SetOrigin( spot + ( 20, 0, 0 ) );
	wait 4;
	check( level.player IsLinked() && level.player2 IsLinked(), "both are seated on the bird (" + level.player IsLinked() + " " + level.player2 IsLinked() + ")" );
	t = 0;
	while ( !flag( "player_on_board_littlebird" ) && t < 20 )
	{
		wait 0.5;
		t += 0.5;
	}
	check( flag( "player_on_board_littlebird" ), "both are on board, the bird may go (" + t + " s)" );
	qa_shot( "seated" );
	report( "seats: player 1 at " + vec_int( level.player.origin ) + ", player 2 at " + vec_int( level.player2.origin ) + ", " + int( Distance( level.player.origin, level.player2.origin ) ) + " apart" );
	wait 8;
	qa_shot( "flying" );
	check( level.player IsLinked() && level.player2 IsLinked(), "both are still seated in flight" );
	report( "done" );
}

// Wolverines!' end (start "convoy"): the convoy arrives, player 1 walks to its parked Humvee and is
// seated on tag_guy1, player 2 on another free seat of the same Humvee; frames of both screens.
invconvoy()
{
	check( level.players.size == 2 && isdefined( level.player2 ), "two players (" + level.players.size + ")" );
	t = 0;
	while ( !flag( "convoy_in_position" ) && t < 120 )
	{
		wait 0.5;
		t += 0.5;
	}
	check( flag( "convoy_in_position" ), "the convoy arrived (" + t + " s)" );
	flag_set( "player_at_convoy" );
	humvee = level.convoy;
	check( isdefined( humvee ), "the player's Humvee exists" );
	if ( !isdefined( humvee ) )
		return;
	t = 0;
	while ( humvee.veh_speed != 0 && t < 40 )
	{
		wait 0.5;
		t += 0.5;
	}
	report( "the Humvee stopped (" + t + " s) at " + vec_int( humvee.origin ) );
	level.player SetOrigin( humvee GetTagOrigin( "tag_guy1" ) + ( 0, 0, 10 ) );
	wait 3;
	check( level.player IsLinked(), "player 1 is seated" );
	check( level.player2 IsLinked(), "player 2 is seated too" );
	report( "player 1 at " + vec_int( level.player.origin ) + ", player 2 at " + vec_int( level.player2.origin ) + ", " + int( Distance( level.player.origin, level.player2.origin ) ) + " apart" );
	qa_shot( "seated" );
	wait 4;
	qa_shot( "later" );
	report( "done" );
}

// Escape from Favela (start "roofrun_bigjump"): player 2 runs up and jumps over the gap; the level plays
// the animated jump for player 1 (his rig) and player 2 plays his own beside it, then both wake up.
bigjump()
{
	check( level.players.size == 2 && isdefined( level.player2 ), "two players (" + level.players.size + ")" );
	wait 8;
	vol = GetEnt( "trig_roofrun_player_bigjump_start", "targetname" );
	edge = GetStruct( "struct_player_bigjump_edge_reference", "targetname" );
	check( isdefined( vol ) && isdefined( edge ), "the jump trigger and the edge exist" );
	if ( !isdefined( vol ) || !isdefined( edge ) )
		return;
	forward = AnglesToForward( edge.angles );
	report( "edge " + vec_int( edge.origin ) + " angles " + edge.angles + ", player 1 at " + vec_int( level.player.origin ) + ", player 2 at " + vec_int( level.player2.origin ) );
	start = edge.origin - forward * 140;
	spot = PlayerPhysicsTrace( start + ( 0, 0, 40 ), start - ( 0, 0, 400 ) );
	level.player2 SetOrigin( spot );
	wait 0.5;
	level.player2 SetPlayerAngles( ( 0, edge.angles[ 1 ], 0 ) );
	wait 0.5;
	report( "player 2 starts at " + vec_int( level.player2.origin ) );
	press( level.player2, "forward", 3000 );
	wait 0.5;
	for ( n = 0; n < 14 && !flag( "roofrun_player_bigjump_start" ); n++ )
	{
		press( level.player2, "gostand", 150 );
		wait 0.1;
		report( "run " + n + ": player 2 at " + vec_int( level.player2.origin ) + " speed " + int( Length( level.player2 GetVelocity() ) ) + " touches " + level.player2 IsTouching( vol ) );
	}
	check( flag( "roofrun_player_bigjump_start" ), "the jump was caught" );
	wait 0.7;
	check( level.player IsLinked() && level.player2 IsLinked(), "both are in their jump rigs (" + level.player IsLinked() + " " + level.player2 IsLinked() + ")" );
	report( "player 1 at " + vec_int( level.player.origin ) + ", player 2 at " + vec_int( level.player2.origin ) + ", " + int( Distance( level.player.origin, level.player2.origin ) ) + " apart" );
	qa_shot( "jump-a" );
	wait 0.6;
	qa_shot( "jump-b" );
	t = 0;
	while ( !flag( "player_recovery_blackscreen" ) && t < 40 )
	{
		wait 0.5;
		t += 0.5;
	}
	check( flag( "player_recovery_blackscreen" ), "the black screen came (" + t + " s)" );
	wait 8;
	qa_shot( "wake-a" );
	t = 0;
	while ( !flag( "player_recovery_done" ) && t < 60 )
	{
		wait 0.5;
		t += 0.5;
	}
	check( flag( "player_recovery_done" ), "the wake-up is over (" + t + " s)" );
	wait 2;
	check( !level.player IsLinked() && !level.player2 IsLinked(), "both are free (" + level.player IsLinked() + " " + level.player2 IsLinked() + ")" );
	report( "after: player 1 at " + vec_int( level.player.origin ) + ", player 2 at " + vec_int( level.player2.origin ) + ", " + int( Distance( level.player.origin, level.player2.origin ) ) + " apart" );
	qa_shot( "up" );
	report( "done" );
}

// Cliffhanger, start "snowmobile": how each player's snowmobile looks to the other (frames of both
// machines; the host's is player 1's, looking at player 2's snowmobile beside him).
snowview()
{
	check( level.players.size == 2 && isdefined( level.player2 ), "two players (" + level.players.size + ")" );
	if ( level.start_point == "icepick" )
	{
		// player 2 rides, player 1 walks up and looks at his snowmobile
		t = 0;
		while ( !flag( "player_snowmobile_available" ) && t < 90 )
		{
			wait 0.5;
			t += 0.5;
		}
		wait 1;
		level.player2 player_mount_vehicle( level.player_snowmobile );
		wait 5;
		v = level.player2.vehicle;
		sp = GetEnt( "playermobile_spawner", "targetname" );
		if ( isdefined( sp ) )
			report( "spawner " + sp.classname + " vehicletype " + sp.vehicletype );
		report( "vehicle type " + v.vehicletype + ", riders " + isdefined( v.riders ) + ", seats " + v maps\_mw2coop::seats_text() );
		report( "player 2 rides model " + v.model + ", first person " + isdefined( v.firstPerson ) + ", copy " + isdefined( v.mw2coop_copy ) );
		// four fixed views around the snowmobile, 130 units away, standing on the snow
		report( "rider body " + isdefined( v.mw2coop_rider ) );
		if ( isdefined( v.mw2coop_rider ) )
			report( "rider at " + v.mw2coop_rider.origin + " tag_driver " + v GetTagOrigin( "tag_driver" ) + " model " + v.mw2coop_rider.model );
		if ( isdefined( v.mw2coop_copy ) )
		{
			d = v.mw2coop_copy GetTagOrigin( "tag_driver" ) - v GetTagOrigin( "tag_driver" );
			report( "tag_driver of the third-person copy minus the vehicle's: " + d + ", copy origin minus vehicle " + ( v.mw2coop_copy.origin - v.origin ) );
		}
		if ( isdefined( v.mw2coop_rider ) )
		{
			r = v.mw2coop_rider;
			center = v GetTagOrigin( "tag_driver" ) + ( 0, 0, 45 );
			// the grips: the first-person hands attached to the snowmobile hold them
			// in a thread of its own: should the vehicle not answer for the hands' bones, only it stops
			v thread report_grips();
			wait 1;
			variant = "seated";
			{
				wait 0.5;
				report( "pose " + variant + ": " + rider_bones( r, v ) );
				// right side and front-left, 110 units away, on the snow
				for ( n = 0; n < 4; n++ )
				{
					dir = AnglesToForward( ( 0, v.angles[ 1 ] - 90 + n * 90, 0 ) );
					spot = center + dir * 110;
					spot = PlayerPhysicsTrace( spot + ( 0, 0, 60 ), spot - ( 0, 0, 300 ) );
					level.player SetOrigin( spot );
					level.player SetPlayerAngles( VectorToAngles( center - level.player GetEye() ) );
					wait 0.8;
					qa_shot( "rider-pose" + variant + "-" + n );
				}
			}
		}
		// Soap keeps his own look (the rider is a copy of his model)
		if ( isdefined( level.price ) )
		{
			report( "soap wears " + level.price.model );
			spot = level.price.origin + AnglesToForward( level.price.angles ) * 90;
			spot = PlayerPhysicsTrace( spot + ( 0, 0, 60 ), spot - ( 0, 0, 300 ) );
			level.player SetOrigin( spot );
			level.player SetPlayerAngles( VectorToAngles( level.price.origin + ( 0, 0, 40 ) - level.player GetEye() ) );
			wait 0.8;
			qa_shot( "soap" );
		}
		report( "done" );
		return;
	}
	wait 6;
	foreach ( player in level.players )
	{
		v = player.vehicle;
		if ( isdefined( v ) )
			report( player_name( player ) + " rides model " + v.model + ", first person " + isdefined( v.firstPerson ) + ", hidden player body " + isdefined( player.mw2coop_x ) );
	}
	d = level.player2.vehicle.origin - level.player.vehicle.origin;
	level.player SetPlayerAngles( VectorToAngles( d ) + ( 10, 0, 0 ) );
	wait 0.5;
	qa_shot( "snow-p1-sees-p2" );
	d = level.player.vehicle.origin - level.player2.vehicle.origin;
	level.player2 SetPlayerAngles( VectorToAngles( d ) + ( 10, 0, 0 ) );
	wait 0.5;
	qa_shot( "snow-p2-sees-p1" );
	report( "done" );
}

// Cliffhanger, start point "icepick": after Soap MacTavish's icepick fight (level.price in the scripts) two snowmobiles wait; player 2
// takes the one the level made for player 1, player 1 takes the other, and the level goes on
// with player 1's as level.player_snowmobile.
snowpick()
{
	check( level.players.size == 2 && isdefined( level.player2 ), "two players (" + level.players.size + ")" );
	t = 0;
	while ( !flag( "player_snowmobile_available" ) && t < 90 )
	{
		wait 0.5;
		t += 0.5;
	}
	check( flag( "player_snowmobile_available" ), "snowmobiles available (" + t + " s)" );
	wait 1;
	check( isdefined( level.mw2coop_partner_snowmobile ), "second snowmobile spawned" );
	level.player2 player_mount_vehicle( level.player_snowmobile );
	wait 1;
	level.player player_mount_vehicle( level.mw2coop_partner_snowmobile );
	t = 0;
	while ( !flag( "player_rides_snowmobile" ) && t < 10 )
	{
		wait 0.25;
		t += 0.25;
	}
	check( flag( "player_rides_snowmobile" ), "the level goes on (player_rides_snowmobile)" );
	check( isdefined( level.player.vehicle ) && level.player_snowmobile == level.player.vehicle, "level.player_snowmobile is player 1's" );
	check( isdefined( level.player2.vehicle ) && level.player2.vehicle != level.player.vehicle, "player 2 on the other snowmobile" );
	report( "done" );
}

// Spawn audit (any map, any start point): where player 2 ends up after the level start, over
// 12 s. PASS when he is alive, up, within 300 units of player 1 and not far below him, and
// either standing on ground or held by something (linked).
bias_group( player )
{
	group = player GetThreatBiasGroup();
	if ( !isdefined( group ) )
		return "none";
	return group;
}

spawn_audit()
{
	check( level.players.size == 2 && isdefined( level.player2 ), "two players (" + level.players.size + ")" );
	starts = "";
	if ( isdefined( level.start_functions ) )
		foreach ( s in level.start_functions )
			starts += s[ "name" ] + " ";
	mode = "undefined";
	if ( isdefined( level.level_mode ) )
		mode = level.level_mode;
	report( "start point " + level.start_point + ", level_mode " + mode + ", starts: " + starts );
	samples = [];
	samples[ 0 ] = 0.5;
	samples[ 1 ] = 3;
	samples[ 2 ] = 10.5;
	waited = 0;
	foreach ( at in samples )
	{
		wait at - waited;
		waited = at;
		report( "t+" + ( at + 1.5 ) + " " + spawn_text() );
	}
	p1 = level.player;
	p2 = level.player2;
	d = Distance( p1.origin, p2.origin );
	dz = p2.origin[ 2 ] - p1.origin[ 2 ];
	check( IsAlive( p2 ) && !maps\_mw2coop::is_downed( p2 ), "player 2 alive and up" );
	check( d < 300, "player 2 near player 1 (" + int( d ) + " units)" );
	check( dz > -150, "player 2 not below player 1 (dz " + int( dz ) + ")" );
	check( maps\_mw2coop::is_standing( p2 ) || p2 IsLinked(), "player 2 on ground or held" );
	kit_checks();
	report( "done" );
}

spawn_text()
{
	p1 = level.player;
	p2 = level.player2;
	return "start " + level.start_point + ", d " + int( Distance( p1.origin, p2.origin ) ) + ", dz " + int( p2.origin[ 2 ] - p1.origin[ 2 ] )
		+ ", p1 " + int( p1.origin[ 0 ] ) + "," + int( p1.origin[ 1 ] ) + "," + int( p1.origin[ 2 ] ) + " linked " + p1 IsLinked() + " standing " + maps\_mw2coop::is_standing( p1 )
		+ ", p2 linked " + p2 IsLinked() + " standing " + maps\_mw2coop::is_standing( p2 ) + " alive " + IsAlive( p2 );
}

// Takedown opening car (default start). Names no favela script (they are not in other zones):
// the bot ducks through the crouch command's notify, as NotifyOnPlayerCommand would send it.
takedown()
{
	check( level.players.size == 2 && isdefined( level.player2 ), "two players (" + level.players.size + ")" );
	wait 2;
	p1 = level.player;
	p2 = level.player2;
	check( p1 IsLinked() && p2 IsLinked(), "both in the car (linked " + p1 IsLinked() + " " + p2 IsLinked() + ")" );
	d = Distance( p1 GetEye(), p2 GetEye() );
	check( d > 20 && d < 80, "player 2 on the back seat (eyes " + int( d ) + " units apart)" );
	flag_wait( "car_getting_shot" );
	if ( GetDvar( "mw2coop_qa" ) == "takedown2" )
	{
		p1 notify( "go_crouch" );
		for ( t = 0; t < 8 && !isdefined( p2.mw2coop_shot ); t += 0.1 )
			wait 0.1;
		check( isdefined( p2.mw2coop_shot ) && !isdefined( p1.mw2coop_shot ), "Makarov hits player 2 (standing), not player 1 (ducking)" );
		report( "done" );
		return;
	}
	foreach ( player in level.players )
		player notify( "go_crouch" );
	wait 1;
	check( isdefined( p1.mw2coop_ducking ) && p1.mw2coop_ducking && isdefined( p2.mw2coop_ducking ) && p2.mw2coop_ducking, "both duck" );
	flag_wait( "start_chase" );
	check( !isdefined( p1.mw2coop_shot ) && !isdefined( p2.mw2coop_shot ), "nobody shot while ducking" );
	foreach ( player in level.players )
		player notify( "go_stand" );
	for ( t = 0; t < 10 && ( p1 IsLinked() || p2 IsLinked() ); t += 0.25 )
		wait 0.25;
	wait 0.5;
	check( !p1 IsLinked() && !p2 IsLinked(), "both out of the car" );
	d = Distance( p1.origin, p2.origin );
	check( d < 120 && abs( p1.origin[ 2 ] - p2.origin[ 2 ] ) < 30, "out next to each other (" + int( d ) + " units)" );
	check( IsAlive( p1 ) && IsAlive( p2 ), "both alive" );
	report( "done" );
}

// The Only Easy Day... opening (default start). Names no oilrig script (not in other zones).
// The strike is forced through player.mw2coop_qa_melee (the bot cannot aim at the guard).
oilrig()
{
	check( level.players.size == 2 && isdefined( level.player2 ), "two players (" + level.players.size + ")" );
	p1 = level.player;
	p2 = level.player2;
	flag_wait( "player_attached_to_sdv" );
	wait 1;
	check( p1 IsLinked() && p2 IsLinked(), "both on an SDV (linked " + p1 IsLinked() + " " + p2 IsLinked() + ")" );
	d = Distance( p1.origin, p2.origin );
	report( "underwater, players " + int( d ) + " units apart" );
	flag_wait( "player_is_done_swimming" );
	wait 2;
	d = Distance( p1.origin, p2.origin );
	check( p1 IsLinked() && p2 IsLinked() && d < 600, "both surfaced and swimming (" + int( d ) + " units apart, p1 " + p1.origin + " p2 " + p2.origin + ")" );
	killer = p1;
	other = p2;
	if ( GetDvar( "mw2coop_qa" ) == "oilrig2" )
	{
		killer = p2;
		other = p1;
	}
	wait 3;
	killer.mw2coop_qa_melee = true;
	for ( t = 0; t < 60 && !flag( "player_done_being_helped_from_water" ); t += 0.25 )
		wait 0.25;
	check( flag( "player_done_being_helped_from_water" ), "the killer is out of the water" );
	for ( t = 0; t < 40 && !isdefined( other.mw2coop_out_of_water ); t += 0.25 )
		wait 0.25;
	check( isdefined( other.mw2coop_out_of_water ), "the other player is pulled out after him (" + t + " s)" );
	wait 1;
	d = Distance( p1.origin, p2.origin );
	check( !p1 IsLinked() && !p2 IsLinked() && d < 300 && abs( p1.origin[ 2 ] - p2.origin[ 2 ] ) < 30, "both out on the platform together (" + int( d ) + " units, p1 " + p1.origin + " linked " + p1 IsLinked() + ", p2 " + p2.origin + " linked " + p2 IsLinked() + ")" );
	check( IsAlive( p1 ) && IsAlive( p2 ), "both alive" );
	report( "done" );
}

// Second Sun opening (default start). Names no dcemp script (not in other zones).
dcemp()
{
	check( level.players.size == 2 && isdefined( level.player2 ), "two players (" + level.players.size + ")" );
	p1 = level.player;
	p2 = level.player2;
	flag_wait( "crash_actors_ready" );
	wait 0.5;
	d = Distance( p1 GetEye(), p2 GetEye() );
	check( p1 IsLinked() && p2 IsLinked() && d > 16 && d < 90, "both in the wreck (eyes " + int( d ) + " units apart)" );
	flag_wait( "give_player_weapon" );
	wait 1;
	check( p1 HasWeapon( "m4m203_eotech" ) && p2 HasWeapon( "m4m203_eotech" ), "both got a rifle" );
	if ( level.script != "dcemp" )
	{
		// dcburning ends here: the crash site is its last scene
		flag_wait( "end_sequence_starting" );
		check( p1 IsLinked() && p2 IsLinked(), "both still in the wreck at the end" );
		report( "done" );
		return;
	}
	flag_wait( "iss_fx" );
	wait 3;
	d = Distance( p1 GetEye(), p2 GetEye() );
	check( p1 IsLinked() && p2 IsLinked() && d < 150, "both at the ISS (" + int( d ) + " units apart)" );
	flag_wait( "iss_done" );
	wait 4;
	d = Distance( p1 GetEye(), p2 GetEye() );
	check( p1 IsLinked() && p2 IsLinked() && d < 120, "both back in the wreck (" + int( d ) + " units apart)" );
	for ( t = 0; t < 60 && ( p1 IsLinked() || p2 IsLinked() ); t += 0.25 )
		wait 0.25;
	wait 1;
	d = Distance( p1.origin, p2.origin );
	check( !p1 IsLinked() && !p2 IsLinked() && d < 150, "both out of the wreck (" + int( d ) + " units, after " + t + " s)" );
	check( IsAlive( p1 ) && IsAlive( p2 ), "both alive" );
	report( "done" );
}

// Second Sun street (start "street"). Names no dcemp script (not in other zones).
street()
{
	check( level.players.size == 2 && isdefined( level.player2 ), "two players (" + level.players.size + ")" );
	p1 = level.player;
	p2 = level.player2;
	hide = getEntArrayWithFlag( "street_player_hide" );
	check( hide.size > 0, "hiding place trigger found (" + hide.size + ")" );
	if ( !hide.size )
		return;
	spot = hide[ 0 ].origin;
	foreach ( guy in level.team )
		guy ent_flag_wait( "street_hide" );
	report( "team hidden, hiding place at " + spot );
	level.mw2coop_no_follow = true;	// the bot teleports player 1: player 2 must not follow
	p1 SetOrigin( PlayerPhysicsTrace( spot + ( 0, 0, 40 ), spot - ( 0, 0, 200 ) ) );
	wait 2;
	check( isdefined( p1.mw2coop_street_hidden ) && !flag( "street_safe" ), "player 1 hidden, street not safe while player 2 is out" );
	p2 SetOrigin( PlayerPhysicsTrace( spot + ( 24, 0, 40 ), spot + ( 24, 0, -200 ) ) );
	for ( t = 0; t < 5 && !flag( "street_safe" ); t += 0.1 )
		wait 0.1;
	check( flag( "street_safe" ), "street safe once both are hidden" );
	wait 3;
	check( IsAlive( p1 ) && IsAlive( p2 ), "both alive" );
	report( "done" );
}

// No Russian ending (start "escape"). Names no airport script (not in other zones): the bot puts
// both players by the van's ending node and watches Makarov's shots.
nrend()
{
	check( level.players.size == 2 && isdefined( level.player2 ), "two players (" + level.players.size + ")" );
	p1 = level.player;
	p2 = level.player2;
	node = getstruct( "escape_ending_node", "targetname" );
	check( isdefined( node ), "ending node found" );
	level.mw2coop_no_follow = true;
	// follow Makarov along the escape (he waits for the player), then stand by the van
	for ( t = 0; t < 150 && !flag( "escape_sequence_go" ); t += 0.5 )
	{
		foreach ( i, player in level.players )
		{
			if ( player IsLinked() || !isdefined( level.makarov ) )
				continue;
			if ( !flag( "escape_sequence_reach" ) )
			{
				back = AnglesToForward( ( 0, level.makarov.angles[ 1 ], 0 ) ) * -140;
				right = AnglesToRight( ( 0, level.makarov.angles[ 1 ], 0 ) ) * ( 40 * ( i * 2 - 1 ) );
				at = level.makarov.origin + back + right;
				if ( Distance( player.origin, at ) > 220 )
					player SetOrigin( PlayerPhysicsTrace( at + ( 0, 0, 40 ), at - ( 0, 0, 100 ) ) );
			}
			else if ( GetDvar( "mw2coop_qa" ) == "nrend2" && player == level.player && isdefined( level.escape_van_dummy ) )
			{
				// where the close ending's player rig starts, facing the van
				van = level.escape_van_dummy;
				anime = level.scr_anim[ "player_ending" ][ "end_player_shot" ];
				at = GetStartOrigin( van GetTagOrigin( "origin_animate_jnt" ), van GetTagAngles( "origin_animate_jnt" ), anime );
				if ( Distance( player.origin, at ) > 20 )
					player SetOrigin( PlayerPhysicsTrace( at + ( 0, 0, 40 ), at - ( 0, 0, 100 ) ) );
				player SetPlayerAngles( VectorToAngles( ( van.origin[ 0 ], van.origin[ 1 ], at[ 2 ] ) - at ) );
			}
			else
			{
				right = AnglesToRight( ( 0, node.angles[ 1 ], 0 ) );
				at = node.origin + right * ( 50 * ( i * 2 - 1 ) );
				if ( Distance( player.origin, at ) > 60 )
					player SetOrigin( PlayerPhysicsTrace( at + ( 0, 0, 40 ), at - ( 0, 0, 100 ) ) );
			}
		}
		wait 0.5;
	}
	check( flag( "escape_sequence_go" ), "the ending starts (" + t + " s)" );
	for ( t = 0; t < 60 && ( !isdefined( level.mw2coop_ending_left ) || level.mw2coop_ending_left > 0 ); t += 0.25 )
		wait 0.25;
	check( isdefined( level.mw2coop_ending_left ) && level.mw2coop_ending_left == 0, "Makarov shot both players (" + t + " s)" );
	wait 3;
	check( p1 IsLinked() && p2 IsLinked(), "both down (linked " + p1 IsLinked() + " " + p2 IsLinked() + ")" );
	report( "health p1 " + p1.health + " p2 " + p2.health );
	report( "done" );
}

// Endgame knife fight (ending map, start "wakeup"). Names no knife fight script (not in other
// zones): the bot strikes through player.mw2coop_qa_melee, holds the crawl and Use buttons
// through player.mw2coop_qa_hold and throws with the "throw" notify. Only the striker is moved
// (to Shepherd); the other player is left where he got up.
knife()
{
	check( level.players.size == 2 && isdefined( level.player2 ), "two players (" + level.players.size + ")" );
	striker = level.player;
	if ( GetDvar( "mw2coop_qa" ) == "knife2" )
		striker = level.player2;
	other = level.player;
	if ( striker == level.player )
		other = level.player2;
	foreach ( player in level.players )
	{
		player.mw2coop_qa_hold = [];
		player.mw2coop_qa_hold[ "use" ] = false;
	}
	wait 2;
	d = Distance( level.player GetEye(), level.player2 GetEye() );
	check( level.player IsLinked() && level.player2 IsLinked() && d < 140, "both wake up in the wreck (" + int( d ) + " units apart)" );
	flag_wait( "player_standing" );
	wait 2;
	check( !level.player IsLinked() && !level.player2 IsLinked(), "both up after the wake-up" );
	level.mw2coop_no_follow = true;
	for ( t = 0; t < 60 && !isdefined( level.shepherd ); t += 0.5 )
		wait 0.5;
	check( isdefined( level.shepherd ), "Shepherd there" );
	// Shepherd at the truck is hidden until the stumbling Shepherd walked there: a strike now
	// must not count (the scene would play with an invisible Shepherd)
	for ( t = 0; t < 2 && shepherd_hidden(); t += 0.25 )
		knife_strike_try( striker );
	check( !flag( "turn_buckle_start" ), "no strike while Shepherd is still hidden (hidden " + shepherd_hidden() + ")" );
	// the striker waits near the truck where he cannot see the stumbler (he stays the lead:
	// nearer to Shepherd), so only the other player's sight can start him
	for ( t = 0; t < 20 && !isdefined( level.shepherd_stumble ); t += 0.5 )
		wait 0.5;
	wait_spot = undefined;
	if ( isdefined( level.shepherd_stumble ) )
		wait_spot = sight_spot( level.shepherd_stumble, level.shepherd.origin, "150 250 350", false );
	check( isdefined( wait_spot ), "a spot by the truck without sight of the stumbling Shepherd" );
	if ( isdefined( wait_spot ) )
		striker SetOrigin( wait_spot );
	// a player walking on sets the map's trigger flag shepherd_can_run; the bot does not walk
	if ( shepherd_hidden() && flag_exist( "shepherd_can_run" ) && !flag( "shepherd_can_run" ) )
	{
		report( "QA sets shepherd_can_run (the bot does not walk)" );
		flag_set( "shepherd_can_run" );
	}
	// the stumbler runs off when a player sees him: the other player goes where he would see
	// him coming over the dunes, so the co-op sight check is what starts him
	level.mw2coop_qa_stumbler_runs = false;
	level thread stumbler_runs_watch();
	spot = undefined;
	if ( isdefined( level.shepherd_stumble ) )
		spot = sight_spot( level.shepherd_stumble, level.shepherd_stumble.origin, "150 250 350 500 700", true );
	check( isdefined( spot ), "a spot where the stumbling Shepherd can be seen" );
	if ( isdefined( spot ) )
	{
		other SetOrigin( spot );
		other SetPlayerAngles( VectorToAngles( level.shepherd_stumble GetEye() - other GetEye() ) );
		report( "other player at " + spot + ", " + int( Distance( spot, level.shepherd_stumble.origin ) ) + " units from the stumbler; sight: lead" + sight_text( level.mw2coop_lead, level.shepherd_stumble ) + " / other" + sight_text( other, level.shepherd_stumble ) );
	}
	for ( t = 0; t < 10 && shepherd_hidden() && !level.mw2coop_qa_stumbler_runs; t += 0.5 )
		wait 0.5;
	other thread watch_stumbler();
	check( level.mw2coop_qa_stumbler_runs, "the stumbling Shepherd runs off when the other player sees him (" + t + " s)" );
	for ( t = 0; t < 90 && shepherd_hidden(); t += 0.5 )
		wait 0.5;
	check( !shepherd_hidden(), "the stumbling Shepherd reached the truck (" + t + " s)" );
	for ( t = 0; t < 60 && !flag( "turn_buckle_start" ); t += 0.25 )
		knife_strike_try( striker );
	check( flag( "turn_buckle_start" ), "a player strikes Shepherd (" + t + " s)" );
	check( level.mw2coop_lead == striker, "the striker plays the scene" );
	wait 2.5;
	check( icon_hidden( striker ) && icon_hidden( other ) && GetDvar( "mw2coop_hide_names" ) == "1", "no partner icon or name on either screen" );
	check( isdefined( other.laststand ) && other.laststand && isdefined( other.mw2coop_awake ) && !other.mw2coop_awake, "the other player is down and out" );
	d = Distance( other.origin, level.shepherd.origin );
	check( d < 400, "and lies near the fight (" + int( d ) + " units from Shepherd)" );
	level thread knife_watch_report( other );

	// crawl to the revolver: alternate the two crawl buttons
	report( "crawl" );
	side = "left";
	for ( t = 0; t < 200 && !flag( "player_looks_at_knife" ) && !flag( "player_uses_knife" ); t += 0.15 )
	{
		striker.mw2coop_qa_hold[ "left" ] = side == "left";
		striker.mw2coop_qa_hold[ "right" ] = side == "right";
		if ( side == "left" )
			side = "right";
		else
			side = "left";
		wait 0.15;
	}
	striker.mw2coop_qa_hold[ "left" ] = false;
	striker.mw2coop_qa_hold[ "right" ] = false;
	report( "knife: tap Use (" + t + " s)" );
	level.mw2coop_qa_knife_out = false;
	level thread knife_out_watch();
	// pull the knife: many quick Use presses
	for ( t = 0; t < 120 && !level.mw2coop_qa_knife_out && !flag( "shepherd_killed" ); t += 0.1 )
	{
		striker.mw2coop_qa_hold[ "use" ] = !striker.mw2coop_qa_hold[ "use" ];
		wait 0.1;
	}
	striker.mw2coop_qa_hold[ "use" ] = false;
	report( "throw (" + t + " s)" );
	// aim at Shepherd and throw
	other.mw2coop_saw_kill = false;
	for ( t = 0; t < 60 && !flag( "shepherd_killed" ); t += 0.1 )
	{
		if ( other.mw2coop_awake )
			other.mw2coop_saw_kill = true;
		if ( isdefined( level.shepherd ) )
			striker SetPlayerAngles( VectorToAngles( level.shepherd GetTagOrigin( "tag_eye" ) - striker GetEye() ) );
		if ( flag( "player_aims_knife_at_shepherd" ) )
			striker notify( "throw" );
		wait 0.1;
	}
	check( flag( "shepherd_killed" ), "Shepherd killed by the striker's knife (" + t + " s)" );
	check( other.mw2coop_awake, "the other player is awake as Shepherd dies" );
	wait 3;
	check( other.mw2coop_awake, "and still awake 3 s later" );
	wait 27;	// Price wakes up: the witness stays awake
	check( other.laststand && IsAlive( other ), "the other player watched the whole fight, down" );
	report( "done" );
}

// shep_trace_passed()'s four traces from a player to the stumbler: fraction and surface.
sight_text( player, stumbler )
{
	text = "";
	for ( offset = 0; offset <= 48; offset += 16 )
	{
		trace = BulletTrace( player.origin + ( 0, 0, 32 ), stumbler GetEye() + ( 0, 0, offset ), false, undefined );
		text += " " + int( trace[ "fraction" ] * 100 ) + trace[ "surfacetype" ];
	}
	return text;
}

// self: a player; he keeps his eyes on the running stumbler (screenshots of him).
watch_stumbler()
{
	for ( t = 0; t < 12 && shepherd_hidden() && isdefined( level.shepherd_stumble ); t += 0.05 )
	{
		self SetPlayerAngles( VectorToAngles( level.shepherd_stumble GetEye() - self GetEye() ) );
		wait 0.05;
	}
}

icon_hidden( player )
{
	return !isdefined( player.friendlyIcon ) || player.friendlyIcon.alpha == 0;
}

// Price's eyes, the passenger's body's eyes and the passenger's own eyes in the boat's frame
// (forward, right, up), with the poses.
boat_poses_text( other )
{
	text = "Price " + level.price.a.boat_pose + " eye " + boat_local( level.price GetTagOrigin( "tag_eye" ) ) + bones_text( level.price );
	if ( isdefined( level.mw2coop_boat_body ) )
		text += ", body " + level.mw2coop_boat_body.mw2coop_pose + " eye " + boat_local( level.mw2coop_boat_body GetTagOrigin( "tag_eye" ) ) + bones_text( level.mw2coop_boat_body );
	text += ", passenger eye " + boat_local( other GetEye() );
	// where the guns and the view point, degrees from the bow, to the left positive
	text += "; yaw: Price gun " + gun_yaw( level.price ) + " (his aim " + int( level.price.a.boatAimYaw ) + ")";
	if ( isdefined( level.mw2coop_boat_body ) )
		text += ", body gun " + gun_yaw( level.mw2coop_boat_body );
	return text + ", passenger view " + view_yaw( other );
}

view_yaw( player )
{
	return int( AngleClamp180( player GetPlayerAngles()[ 1 ] - level.players_boat.angles[ 1 ] ) );
}

gun_follows_view( player )
{
	return isdefined( level.mw2coop_boat_body ) && abs( AngleClamp180( gun_yaw( level.mw2coop_boat_body ) - view_yaw( player ) ) ) < 8;
}

gun_yaw( ent )
{
	return int( AngleClamp180( ent GetTagAngles( "tag_flash" )[ 1 ] - level.players_boat.angles[ 1 ] ) );
}

// Bones every human model has: root, pelvis, head.
bones_text( ent )
{
	return " root " + boat_local( ent GetTagOrigin( "tag_origin" ) ) + " pelvis " + boat_local( ent GetTagOrigin( "j_mainroot" ) ) + " head " + boat_local( ent GetTagOrigin( "j_head" ) );
}

boat_local( pos )
{
	boat = level.players_boat;
	d = pos - boat.origin;
	return int( VectorDot( d, AnglesToForward( boat.angles ) ) ) + " " + int( VectorDot( d, AnglesToRight( boat.angles ) ) ) + " " + int( VectorDot( d, AnglesToUp( boat.angles ) ) );
}

// Price's eyes from the boat's long axis, to the right positive.
price_side()
{
	boat = level.players_boat;
	return VectorDot( level.price GetTagOrigin( "tag_eye" ) - boat.origin, AnglesToRight( boat.angles ) );
}

// The passenger's eyes on the other side of the boat's long axis from Price's (his "left" pose
// lies on the right tube: QA boat measured it).
across_from_price( other )
{
	boat = level.players_boat;
	side = VectorDot( other GetEye() - boat.origin, AnglesToRight( boat.angles ) );
	return side * price_side() < 0 && abs( side ) > 8;
}

// The player's distance from the boat's long axis, to the right positive.
boat_side( player )
{
	boat = level.players_boat;
	return VectorDot( player.origin - boat.origin, AnglesToRight( boat.angles ) );
}

stumbler_runs_watch()
{
	level waittill( "run_shep_run" );
	level.mw2coop_qa_stumbler_runs = true;
}

// A floor spot at one of the distances from center where the stumbler is (or is not) in
// shep_trace_passed()'s sight.
sight_spot( stumbler, center, dists, want_sight )
{
	foreach ( dist in strtok( dists, " " ) )
	{
		for ( yaw = 0; yaw < 360; yaw += 20 )
		{
			at = center + AnglesToForward( ( 0, yaw, 0 ) ) * int( dist );
			floor = PlayerPhysicsTrace( at + ( 0, 0, 150 ), at - ( 0, 0, 300 ) );
			if ( abs( floor[ 2 ] - center[ 2 ] ) > 150 )
				continue;
			if ( sees_stumbler( floor, stumbler ) == want_sight )
				return floor;
		}
	}
	return undefined;
}

sees_stumbler( origin, stumbler )
{
	for ( offset = 0; offset <= 48; offset += 16 )
	{
		trace = BulletTrace( origin + ( 0, 0, 32 ), stumbler GetEye() + ( 0, 0, offset ), false, undefined );
		if ( trace[ "fraction" ] >= 1 || trace[ "surfacetype" ] == "none" )
			return true;
	}
	return false;
}

shepherd_hidden()
{
	return isdefined( level.mw2coop_shepherd_hidden ) && level.mw2coop_shepherd_hidden;
}

// One strike attempt: in front of Shepherd, looking at his eyes, melee (0.25 s).
knife_strike_try( striker )
{
	fwd = AnglesToForward( ( 0, level.shepherd.angles[ 1 ], 0 ) );
	at = level.shepherd.origin + fwd * 70;
	if ( Distance( striker.origin, at ) > 30 )
		striker SetOrigin( PlayerPhysicsTrace( at + ( 0, 0, 40 ), at - ( 0, 0, 80 ) ) );
	striker SetPlayerAngles( VectorToAngles( level.shepherd GetTagOrigin( "tag_eye" ) - striker GetEye() ) );
	striker.mw2coop_qa_melee = true;
	wait 0.1;
	striker.mw2coop_qa_melee = undefined;
	wait 0.15;
}

knife_out_watch()
{
	level waittill( "knife_pulled_out" );
	level.mw2coop_qa_knife_out = true;
}

// Every 10 s: how far the other player's view is from the action and whether he sees it.
knife_watch_report( other )
{
	level endon( "mw2coop_knife_witness_up" );
	for ( ;; )
	{
		wait 10;
		if ( !isdefined( level.shepherd ) )
			continue;
		eye = other GetEye();
		focus = level.shepherd GetTagOrigin( "tag_eye" );
		sees = BulletTracePassed( eye, focus, false, undefined );
		dot = VectorDot( AnglesToForward( other GetPlayerAngles() ), VectorNormalize( focus - eye ) );
		report( "witness: awake " + other.mw2coop_awake + ", " + int( Distance( eye, focus ) ) + " units from Shepherd, line " + sees + ", facing " + int( dot * 100 ) + "%" );
	}
}

// QA body: put player 2 in front of player 1, facing him (the bodies come from _mw2coop);
// screenshots show how each sees the other.
body()
{
	wait 3;
	angles = level.player GetPlayerAngles();
	yaw = angles[ 1 ];
	forward = AnglesToForward( ( 0, yaw, 0 ) );
	level.player2 SetOrigin( level.player.origin + forward * 120 );
	level.player SetPlayerAngles( ( 0, yaw, 0 ) );
	level.player2 SetPlayerAngles( ( 0, yaw + 180, 0 ) );
	report( "body: models set, player 2 in front of player 1" );
	report( "body: weapons " + level.player GetCurrentWeapon() + " / " + level.player2 GetCurrentWeapon() );
	wait 15;
	level.player2 SetStance( "crouch" );
	report( "body: player 2 crouches" );
	wait 25;
	report( "done" );
}


// Endgame boat chase (af_chase, default start). Names no chase script (not in other zones): the
// bot boards through the jetty trigger and drives with level.mw2coop_qa_boat_autopilot.
boat()
{
	check( level.players.size == 2 && isdefined( level.player2 ), "two players (" + level.players.size + ")" );
	driver = level.player;
	if ( GetDvar( "mw2coop_qa" ) == "boat2" )
		driver = level.player2;
	other = maps\_mw2coop::other_player( driver );
	trigger = GetEnt( "boat_mount", "targetname" );
	check( isdefined( trigger ), "the jetty's boat trigger" );
	wait 2;
	driver SetOrigin( PlayerPhysicsTrace( trigger.origin + ( 0, 0, 40 ), trigger.origin - ( 0, 0, 200 ) ) );
	for ( t = 0; t < 3 && !flag( "player_on_boat" ); t += 0.25 )
		wait 0.25;
	if ( !flag( "player_on_boat" ) )
	{
		report( "QA sets off the boat trigger (standing in it did not)" );
		trigger notify( "trigger", driver );
	}
	for ( t = 0; t < 10 && !flag( "player_on_boat" ); t += 0.25 )
		wait 0.25;
	check( flag( "player_on_boat" ), "on the boat (" + t + " s)" );
	wait 1;
	check( level.mw2coop_lead == driver, "the boarding player drives" );
	check( isdefined( driver.drivingVehicle ) && driver.drivingVehicle == level.players_boat, "and is at the wheel" );
	d = Distance( other.origin, level.players_boat.origin );
	// he is seated once Price has settled in the boat (up to 5 s)
	for ( t = 0; t < 7 && !isdefined( level.mw2coop_boat_seat ); t += 0.25 )
		wait 0.25;
	d = Distance( other.origin, level.players_boat.origin );
	check( other IsLinked() && d < 150, "the other player rides in the boat (" + int( d ) + " units from it, seated after " + t + " s)" );
	if ( isdefined( level.mw2coop_qa_boat_autopilot ) )
		level.players_boat thread [[ level.mw2coop_qa_boat_autopilot ]]( 60 );
	// the body's eyes (forward right up in the boat's frame) every 0.5 s
	text = "body samples:";
	for ( t = 0; t < 3; t += 0.5 )
	{
		if ( isdefined( level.mw2coop_boat_body ) )
			text += " " + boat_local( level.mw2coop_boat_body GetTagOrigin( "tag_eye" ) );
		else
			text += " -";
		wait 0.5;
	}
	report( text );
	report( "poses: " + boat_poses_text( other ) );
	check( gun_follows_view( other ), "his body's gun points where he looks (gun " + gun_yaw( level.mw2coop_boat_body ) + ", view " + view_yaw( other ) + ")" );
	check( abs( view_yaw( other ) ) < 10, "he starts looking ahead over the bow (" + view_yaw( other ) + ")" );
	check( across_from_price( other ), "the passenger lies across from Price (" + int( boat_side( other ) ) + ", Price " + int( price_side() ) + ")" );
	// he looks as far right, then left, as he can (screenshots: his body's gun follows)
	foreach ( turn in strtok( "-60 60", " " ) )
	{
		report( "passenger looks " + turn );
		for ( t = 0; t < 3; t += 0.05 )
		{
			other SetPlayerAngles( ( 0, level.players_boat.angles[ 1 ] - 36 + int( turn ), 0 ) );
			wait 0.05;
		}
	}
	check( gun_follows_view( other ), "after looking about, the gun still follows (gun " + gun_yaw( level.mw2coop_boat_body ) + ", view " + view_yaw( other ) + ")" );
	// the level's triggers that would turn Price over (af_chase_code::price_position_switch())
	// leave him on his tube in co-op: set them all off
	switches = GetEntArray( "price_position_switch", "targetname" );
	foreach ( s in switches )
		s notify( "trigger", level.mw2coop_lead );
	wait 3;
	report( "poses: " + boat_poses_text( other ) );
	check( switches.size > 0 && level.price.a.boat_pose == "left" && !isdefined( level.price.scripted_boat_pose ), "Price keeps his tube through the level's " + switches.size + " position triggers" );
	check( across_from_price( other ), "the passenger still lies across from him (" + int( boat_side( other ) ) + ", Price " + int( price_side() ) + ")" );
	price_turns = 0;
	// 30 s: the QA autopilot is slower than a player, and by 35 to 45 s Shepherd's boat gets
	// away and the level fails the mission as it would for a player that slow
	for ( t = 0; t < 30; t += 10 )
	{
		// Price's root and eyes against the tag, and the body's eyes: what moves while riding
		for ( s = 0; s < 10; s += 2 )
		{
			wait 2;
			if ( level.price.a.boat_pose != "left" )
				price_turns++;
			if ( isdefined( level.mw2coop_boat_body ) )
				report( "drift: Price " + level.price.a.boat_pose + " root " + boat_local( level.price.origin ) + " eye " + boat_local( level.price GetTagOrigin( "tag_eye" ) ) + ", body " + level.mw2coop_boat_body.mw2coop_pose + " eye " + boat_local( level.mw2coop_boat_body GetTagOrigin( "tag_eye" ) ) );
		}
		report( "ride " + ( t + 10 ) + " s: boat speed " + int( level.players_boat Vehicle_GetSpeed() ) + ", passenger " + int( Distance( other.origin, level.players_boat.origin ) ) + " units from it, linked " + other IsLinked() + ", health " + other.health + ", enemy boat " + int( Distance( level.players_boat.origin, level.enemy_boat.origin ) ) );
	}
	check( IsAlive( other ) && other IsLinked(), "the passenger rode along" );
	check( price_turns == 0, "Price stayed on his tube the whole ride (" + price_turns + " samples off it)" );
	report( "done" );
}

// The level's loadout (maps\_loadout): player 2 has player 1's weapons and holds the same one.
loadout()
{
	check( level.players.size == 2 && isdefined( level.player2 ), "two players (" + level.players.size + ")" );
	wait 5;
	kit_checks();
	report( "done" );
}

// What the level gave player 1, player 2 has too: weapons (maps\_mw2coop::sync_weapons), the one
// in his hands, the second grenade class (src/mirror.c), the enemies' threat bias group (mirror)
// and threat bias and accuracy against him (maps\_mw2coop::players_threatbias,
// players_attackeraccuracy).
kit_checks()
{
	p1 = level.player;
	p2 = level.player2;
	a = p1 GetCurrentWeapon();
	b = p2 GetCurrentWeapon();
	report( "player 1: " + a + " | " + weapons_text( p1 ) );
	report( "player 2: " + b + " | " + weapons_text( p2 ) );
	check( a == b, "both hold " + a + " (player 2: " + b + ")" );
	missing = "";
	foreach ( weapon in p1 GetWeaponsListAll() )
		if ( !p2 HasWeapon( weapon ) )
			missing += weapon + " ";
	check( missing == "", "player 2 has all of player 1's weapons (missing: " + missing + ")" );
	c1 = p1 GetOffhandSecondaryClass();
	c2 = p2 GetOffhandSecondaryClass();
	check( c1 == c2, "same second grenade class " + c1 + " (player 2: " + c2 + ")" );
	g1 = bias_group( p1 );
	g2 = bias_group( p2 );
	check( g1 == g2, "same threat bias group '" + g1 + "' (player 2: '" + g2 + "')" );
	report( "threatbias " + p1.threatbias + "/" + p2.threatbias + ", attackeraccuracy " + p1.attackeraccuracy + "/" + p2.attackeraccuracy );
}

// --------------------------------------------------------------------------------------------
// S.S.D.D. (trainer): each player takes his own weapons (maps\_mw2coop_trainer). Start point ""
// tests the rifle, "frag" the grenade box, "pit" the pistol. Each player stands by the weapon,
// looks at it and presses Use on his own machine (press()), as the owner did by hand.
pickups()
{
	check( level.players.size == 2 && isdefined( level.player2 ), "two players (" + level.players.size + ")" );
	wait_intro();	// player 1 can take nothing before
	start = GetDvar( "start" );
	if ( start == "frag" )
	{
		level.mw2coop_no_follow = true;
		pickup_frags();
		return;
	}
	if ( start == "pit" )
	{
		level.mw2coop_no_follow = true;
		pickup_table( "pickup_sidearm", "deserteagle", "pistol" );
		report( "done" );
		return;
	}
	flag_wait( "firing_range_initialized" );
	wait 1;
	check( !level.player HasWeapon( "m4_grunt" ) && !level.player2 HasWeapon( "m4_grunt" ), "no rifles at the range yet" );
	// both inside the sandbag bay where COD-MOD puts its players, player 1 on the left
	// (maps\_mw2coop_trainer::range_place_1)
	start_ent = GetEnt( "player_start_range", "targetname" );
	right = AnglesToRight( ( 0, start_ent.angles[ 1 ], 0 ) );
	side1 = int( VectorDot( level.player.origin - start_ent.origin, right ) );
	side2 = int( VectorDot( level.player2.origin - start_ent.origin, right ) );
	check( side1 < side2 - 30, "player 1 left of player 2 at the range (" + side1 + ", " + side2 + " from the start, to the right)" );
	check( Distance2D( level.player2.origin, start_ent.origin ) < 8 && Distance2D( level.player.origin, ( -2515.2, 5190.2, 0 ) ) < 8, "both on their places in the sandbag bay" );
	foreach ( player in level.players )
	{
		check( !qa_stuck( player ), player_name( player ) + " is not stuck in anything solid at his place" );
		check( qa_in_bay( player, start_ent ), player_name( player ) + " is inside the sandbags (" + bay_text( player, start_ent ) + ")" );
	}
	check( abs( level.player.origin[ 2 ] - level.player2.origin[ 2 ] ) < 4, "both on the floor (" + int( level.player.origin[ 2 ] ) + ", " + int( level.player2.origin[ 2 ] ) + ")" );
	// player 2 was brought along to the range: he must stand where he can walk away
	report( "at the range: player 1 " + vec_int( level.player.origin ) + ", player 2 " + vec_int( level.player2.origin ) + ", " + int( Distance( level.player.origin, level.player2.origin ) ) + " apart" );
	level.mw2coop_no_follow = true;
	flag_wait( "foley_anim_pickup_weapon" );
	wait 2;
	pickup_table( "pickup_rifle", "m4_grunt", "rifle" );
	for ( t = 0; t < 90 && !flag( "player_picked_up_rifle" ); t += 0.5 )
		wait 0.5;
	check( flag( "player_picked_up_rifle" ), "the drill goes on once both have rifles (" + t + " s)" );
	report( "done" );
}

// Player 2 takes the weapon first, then player 1 the one that lies there again.
pickup_table( name, weapon, what )
{
	for ( t = 0; t < 60 && !pickup_ready( name ); t += 0.5 )
		wait 0.5;
	check( pickup_ready( name ), "a " + what + " on the table" );
	if ( !pickup_ready( name ) )
		return;
	first = level.mw2coop_pickups[ name ];
	qa_use_take( level.player2, first, weapon );
	check( level.player2 HasWeapon( weapon ) && !level.player HasWeapon( weapon ), "player 2 takes the " + what + ", player 1 has none (" + weapons_text( level.player ) + ")" );
	wait 1;
	check( pickup_ready( name ) && level.mw2coop_pickups[ name ].model != "" && level.mw2coop_pickups[ name ].model != "_obj", "another " + what + " lies on the table (" + pickup_model( name ) + ")" );
	if ( pickup_ready( name ) )
		qa_use_take( level.player, level.mw2coop_pickups[ name ], weapon );
	check( level.player HasWeapon( weapon ) && level.player2 HasWeapon( weapon ), "player 1 takes his own " + what );
	wait 1;
	check( !pickup_ready( name ), "no third " + what + " on the table" );
}

pickup_model( name )
{
	if ( !pickup_ready( name ) )
		return "none";
	return level.mw2coop_pickups[ name ].model;
}

pickup_ready( name )
{
	return isdefined( level.mw2coop_pickups ) && isdefined( level.mw2coop_pickups[ name ] );
}

// The level intro holds both players (maps\_introscreen patch; before, player 2 walked and took
// a pistol under S.S.D.D.'s title): both press Forward under the title and stay, then both walk.
introfreeze()
{
	check( level.players.size == 2 && isdefined( level.player2 ), "two players (" + level.players.size + ")" );
	level.mw2coop_no_follow = true;
	level.mw2coop_qa_intro_over = flag_exist( "introscreen_complete" ) && flag( "introscreen_complete" );
	level thread qa_note_intro_over();
	from = [];
	foreach ( n, player in level.players )
	{
		from[ n ] = player.origin;
		press( player, "forward", 1000 );
	}
	wait 1;
	over = level.mw2coop_qa_intro_over;
	foreach ( n, player in level.players )
	{
		// a player riding something (Takedown's car) is carried along and cannot walk anyway
		if ( player IsLinked() )
			report( player_name( player ) + " rides (linked) under the title: not checked" );
		else
			check( !over && Distance2D( player.origin, from[ n ] ) < 2, player_name( player ) + " stays still on a Forward press under the intro title (" + int( Distance2D( player.origin, from[ n ] ) ) + " units; intro over: " + over + ")" );
	}
	wait_intro();
	wait 1;
	foreach ( player in level.players )
	{
		if ( player IsLinked() )
		{
			report( player_name( player ) + " rides (linked) after the intro: not checked" );
			continue;
		}
		walked = qa_walk( player );	// before the text: GSC evaluates arguments last to first
		check( walked, player_name( player ) + " walks after the intro (" + level.mw2coop_qa_walk + ")" );
	}
	report( "done" );
}

qa_note_intro_over()
{
	level waittill( "introscreen_complete" );
	level.mw2coop_qa_intro_over = true;
}

// Turns him the way with the most free floor (up to 100 units) and presses Forward for 1 s; true
// when he walked at least half of that room (40 units at most). level.mw2coop_qa_walk says how far.
qa_walk( player )
{
	yaw = player GetPlayerAngles()[ 1 ];
	room = 0;
	best = yaw;
	for ( turn = 0; turn < 360; turn += 45 )
	{
		from = player.origin + ( 0, 0, 8 );
		dir = AnglesToForward( ( 0, yaw + turn, 0 ) );
		free = Distance( from, PlayerPhysicsTrace( from, from + dir * 100 ) );
		// only as far as there is floor: Cliffhanger's cave ledge is open towards the drop, and the
		// bot walked off it where a player walks sideways along the ice (owner, 2026-10-22)
		for ( d = 25; d <= free; d += 25 )
		{
			at = from + dir * d;
			if ( at[ 2 ] - PlayerPhysicsTrace( at, at - ( 0, 0, 64 ) )[ 2 ] > 48 )
			{
				free = d - 25;
				break;
			}
		}
		if ( free > room + 1 )
		{
			room = free;
			best = yaw + turn;
		}
	}
	player SetPlayerAngles( ( 0, best, 0 ) );
	wait 0.2;
	start = player.origin;
	press( player, "forward", 1000 );
	wait 1.5;
	moved = Distance2D( player.origin, start );
	level.mw2coop_qa_walk = int( moved ) + " of " + int( room ) + " free units";
	// 15: Cliffhanger's ledge holds the player to a 0.35 walk (maps\_climb::climb_wall), 17-39 units
	return moved > min( 15, room * 0.5 );
}

// --------------------------------------------------------------------------------------------
// A level's sections, one after another (owner, 2026-10-22: on the levels without big scenes run
// the players through the trigger points and look at what they see). Each start point is a
// section; the next one is reached by a restart at it (as a checkpoint restart is).
tour()
{
	starts = tour_starts();
	at = -1;	// the default start comes before the first named one
	foreach ( n, name in starts )
		if ( name == level.start_point )
			at = n;
	report( "tour " + level.script + ": section " + level.start_point + " (" + ( at + 2 ) + " of " + ( starts.size + 1 ) + ")" );
	// back at the same section: the level failed the mission in it (a stealth section that sees
	// the bot, Just Like Old Times barracks) and restarted it; go on with the next one
	if ( GetDvar( "mw2coop_qa_phase" ) == "tour:" + level.start_point )
		report( "the mission failed in section " + level.start_point + " (the level's own fail, e.g. spotted): skipped" );
	else
	{
		SetDvar( "mw2coop_qa_phase", "tour:" + level.start_point );
		tour_section();
	}
	if ( at + 1 < starts.size )
	{
		SetDvar( "start", starts[ at + 1 ] );
		report( "next section " + starts[ at + 1 ] );
		maps\_mw2coop::request( "restart" );
		return;
	}
	SetDvar( "mw2coop_qa_phase", "done" );
	report( "done" );
}

// cptour: (any map, default start) for every start point of the level: the section starts, the mission is
// failed after a few seconds, and the level must start again at the same section with both players up
// and together; then the next section. The restart from a checkpoint (the section the level reached)
// is this same path.
cptour()
{
	starts = tour_starts();
	at = -1;	// the default start comes before the first named one
	foreach ( n, name in starts )
		if ( name == level.start_point )
			at = n;
	here = level.start_point;
	phase = GetDvar( "mw2coop_qa_phase" );
	// the section the mission was failed in: the level may restart at its own checkpoint for it (Favela's
	// "torture" restarts at "favela"), which is the next section to count from, not a new section to fail in
	was = here;
	if ( GetSubStr( phase, 0, 4 ) == "cpt:" && IsSubStr( phase, ":failed" ) )
	{
		parts = StrTok( phase, ":" );
		if ( parts.size == 3 )
			was = parts[ 1 ];
	}
	if ( phase == "cpt:" + was + ":failed" )
	{
		foreach ( n, name in starts )
			if ( name == was )
				at = n;
		extra = "";
		if ( was != here )
			extra = ", the level's checkpoint for " + was;
		report( "cptour " + level.script + ": restarted at " + here + extra + " (" + ( at + 2 ) + " of " + ( starts.size + 1 ) + ")" );
		SetDvar( "mw2coop_qa_phase", "cpt:" + was + ":ok" );
		if ( flag_exist( "mw2coop_regrouped" ) )
			for ( t = 0; t < 10 && !flag( "mw2coop_regrouped" ); t += 0.5 )
				wait 0.5;
		wait 3;
		check( level.players.size == 2 && isdefined( level.player2 ), "two players after the restart (" + level.players.size + ")" );
		if ( was == here )
			check( GetDvar( "start" ) == here || ( here == "default" && ( GetDvar( "start" ) == "" || GetDvar( "start" ) == "default" ) ) || IsSubStr( GetDvar( "start" ), " ** " ), "the start point is still " + here + " ('" + GetDvar( "start" ) + "')" );
		tour_state( "after the restart" );
		qa_shot( "c-restart" );
	}
	else if ( phase == "cpt:" + here + ":ok" )
		report( "the mission failed again in section " + here + " (the level's own fail): skipped" );
	else
	{
		report( "cptour " + level.script + ": section " + here + " (" + ( at + 2 ) + " of " + ( starts.size + 1 ) + ")" );
		if ( flag_exist( "mw2coop_regrouped" ) )
			for ( t = 0; t < 10 && !flag( "mw2coop_regrouped" ); t += 0.5 )
				wait 0.5;
		if ( level.start_point == "default" )
			wait_intro();
		wait 3;
		check( level.players.size == 2 && isdefined( level.player2 ), "two players (" + level.players.size + ")" );
		SetDvar( "mw2coop_qa_phase", "cpt:" + here + ":failed" );
		report( "failing the mission at " + here );
		level thread maps\_mw2coop::mission_failed();
		wait 40;
		report( "FAIL no restart 40 s after the failure at " + here );
		return;
	}
	if ( at + 1 < starts.size )
	{
		SetDvar( "start", starts[ at + 1 ] );
		report( "next section " + starts[ at + 1 ] );
		maps\_mw2coop::request( "restart" );
		return;
	}
	SetDvar( "mw2coop_qa_phase", "done" );
	report( "done" );
}

// the level's playable start points in order (debug and tweak starts left out, each name once)
tour_starts()
{
	names = [];
	seen = [];
	foreach ( start in level.start_functions )
	{
		name = start[ "name" ];
		if ( !isdefined( seen[ name ] ) && !maps\_mw2coop::start_is_dev( name ) )
			names[ names.size ] = name;
		seen[ name ] = true;
	}
	return names;
}

tour_section()
{
	if ( flag_exist( "mw2coop_regrouped" ) )
		for ( t = 0; t < 10 && !flag( "mw2coop_regrouped" ); t += 0.5 )
			wait 0.5;
	if ( level.start_point == "default" )
		wait_intro();
	wait 2;
	check( level.players.size == 2 && isdefined( level.player2 ), "two players (" + level.players.size + ")" );
	tour_state( "spawn" );
	qa_shot( "a-spawn" );
	foreach ( player in level.players )
	{
		if ( player IsLinked() || !IsAlive( player ) )
			continue;
		check( !qa_stuck( player ), player_name( player ) + " is not stuck at the section's start (" + vec_int( player.origin ) + ")" );
		walked = qa_walk( player );	// before the text: GSC evaluates arguments last to first
		check( walked, player_name( player ) + " can walk (" + level.mw2coop_qa_walk + ")" );
	}
	wait 8;	// the section's own opening (allies move, enemies come)
	tour_state( "after 10 s" );
	qa_shot( "b-10s" );
	foreach ( n, trigger in tour_triggers( 2 ) )
	{
		label = "trigger " + ( n + 1 ) + " (" + trigger_text( trigger ) + ")";
		if ( level.player IsLinked() )
			break;
		level.player SetOrigin( trigger.mw2coop_floor );
		wait 4;	// player 2 follows a jump of player 1 (maps\_mw2coop::follow_leader_teleports)
		tour_state( label );
		qa_shot( "c-trigger" + ( n + 1 ) );
	}
}

// both players' state at a stop: up, apart, held by something
tour_state( where )
{
	text = where + ":";
	foreach ( player in level.players )
	{
		text += " " + player_name( player ) + " " + vec_int( player.origin );
		if ( !IsAlive( player ) )
			text += " DEAD";
		else if ( maps\_mw2coop::is_downed( player ) )
			text += " DOWN";
		if ( player IsLinked() )
			text += " linked";
	}
	apart = int( Distance( level.player.origin, level.player2.origin ) );
	// the Museum's black screens (level.black for everyone, the extra ones for the other players)
	if ( isdefined( level.black ) )
		text += " [black " + level.black.alpha + "]";
	if ( isdefined( level.mw2coop_museum_black ) )
		foreach ( overlay in level.mw2coop_museum_black )
			text += " [p2 black " + overlay.alpha + "]";
	report( text + "; " + apart + " apart" );
	check( IsAlive( level.player ) && IsAlive( level.player2 ) && !maps\_mw2coop::is_downed( level.player ) && !maps\_mw2coop::is_downed( level.player2 ), "both up " + where );
	// both riding something (separate vehicles, separate rigs) may be far apart; one riding and
	// the other left behind may not (dcburning heliride2: player 2 stayed at the level start)
	check( apart < 1500 || ( level.player IsLinked() && level.player2 IsLinked() ), "together " + where + " (" + apart + " units)" );
}

// the level's triggers 300 to 2500 units from player 1, nearest first: story triggers (they set a
// flag, script_flag) not yet passed, enemy spawn triggers and plain ones (dc_whitehouse has 27, 21
// and 32 of them, their origins at their centres)
tour_triggers( count )
{
	found = [];
	foreach ( class in strtok( "trigger_multiple_flag_set trigger_multiple_spawn trigger_multiple", " " ) )
		foreach ( trigger in GetEntArray( class, "classname" ) )
		{
			if ( trigger.origin == ( 0, 0, 0 ) )
				continue;
			if ( isdefined( trigger.script_flag ) && flag_exist( trigger.script_flag ) && flag( trigger.script_flag ) )
				continue;
			d = Distance( trigger.origin, level.player.origin );
			if ( d < 300 || d > 2500 )
				continue;
			// floor under its centre, free to stand on (a centre inside a wall or over a drop sent
			// player 1 6500 units down in the Gulag tour)
			floor = PlayerPhysicsTrace( trigger.origin + ( 0, 0, 48 ), trigger.origin - ( 0, 0, 256 ) );
			if ( floor[ 2 ] < trigger.origin[ 2 ] - 255 || qa_spot_stuck( floor ) )
				continue;
			trigger.mw2coop_floor = floor;
			found[ found.size ] = trigger;
		}
	picked = [];
	while ( picked.size < count && found.size )
	{
		best = 0;
		foreach ( n, trigger in found )
			if ( Distance( trigger.origin, level.player.origin ) < Distance( found[ best ].origin, level.player.origin ) )
				best = n;
		picked[ picked.size ] = found[ best ];
		found = array_remove_index( found, best );
	}
	return picked;
}

trigger_text( trigger )
{
	text = trigger.classname;
	if ( isdefined( trigger.script_flag ) )
		text += " " + trigger.script_flag;
	else if ( isdefined( trigger.targetname ) )
		text += " " + trigger.targetname;
	return text;
}

// A view from outside for the host's screen (player 1): his camera is moved to a point 80 units behind and
// 60 to the left of player 2, looking at him; whatever is linked to them in the scene stays there. The frame
// shows whether the two players' rigs and bodies overlap. Not for the client's screen.
qa_outside( label )
{
	p2 = level.player2;
	angles = ( 0, p2 GetPlayerAngles()[ 1 ], 0 );
	spot = p2.origin + AnglesToForward( angles ) * -80 + AnglesToRight( angles ) * -60 + ( 0, 0, 30 );
	cam = Spawn( "script_origin", spot );
	cam.angles = VectorToAngles( p2.origin + ( 0, 0, 20 ) - spot );
	level.player PlayerLinkTo( cam );
	wait 0.5;
	report( label + ": camera " + vec_int( spot ) + ", players " + int( Distance( level.player.origin, level.player2.origin ) ) + " apart" );
	qa_shot( label );
}

// both machines save their own frame (src/shot.c): <map>-<section>-<label>
qa_shot( label )
{
	SetDvar( "mw2coop_qa_shot", level.script + "-" + level.start_point + "-" + label );
	wait 0.6;
}

// Loose Ends' ending (estate.gsc ending_moments(), maps\_mw2coop_estate): both players go through it
// as Roach, at the same place, held there (Ghost's drag, the LZ, the body toss); Shepherd's shot
// hits both; nobody goes down; the mission ends. Logs both every 5 s.
betrayal()
{
	check( level.players.size == 2 && isdefined( level.player2 ), "two players (" + level.players.size + ")" );
	foreach ( player in level.players )
		player thread betrayal_hits();
	phases = strtok( "play_ending_sequence begin_ending_music enter_the_littlebirds made_it_to_lz begin_overlapped_gasoline_sequence end_the_mission", " " );
	last = "";
	apart = 0;
	free = 0;
	down = 0;
	for ( t = 0; t < 150 && !flag( "end_the_mission" ); t++ )
	{
		phase = "";
		foreach ( f in phases )
			if ( flag_exist( f ) && flag( f ) )
				phase = f;
		if ( phase != last )
			report( "phase " + phase + " at " + t + " s" );
		last = phase;
		// from 2 s into the ending (the cut to black) both are held on Roach's view
		if ( t >= 4 )
		{
			apart = max( apart, Distance( level.player.origin, level.player2.origin ) );
			foreach ( player in level.players )
			{
				if ( !player IsLinked() )
					free++;
				if ( maps\_mw2coop::is_downed( player ) )
					down++;
			}
		}
		if ( t % 5 == 0 )
		{
			text = "t" + t;
			foreach ( player in level.players )
			{
				text += " | " + player_name( player ) + " " + vec_int( player.origin ) + " hp " + player.health;
				if ( player IsLinked() )
					text += " linked";
				text += " " + player GetCurrentWeapon();
			}
			maps\_mw2coop::state( text );
		}
		wait 1;
	}
	check( flag( "end_the_mission" ), "the ending ran to the mission's end (" + t + " s)" );
	check( apart < 2, "both players at Roach's place all through (at most " + int( apart ) + " units apart)" );
	check( free == 0, "both held on the view all through (" + free + " samples free)" );
	check( down == 0, "nobody went down (" + down + " samples)" );
	foreach ( player in level.players )
		check( isdefined( player.mw2coop_qa_low ) && player.mw2coop_qa_low <= 50, "Shepherd's shot hits " + player_name( player ) + " (health down to " + player.mw2coop_qa_low + ")" );
	SetDvar( "mw2coop_qa_phase", "done" );	// the next mission does not rerun it
	report( "done" );
}

// his lowest health after the LZ (Shepherd's pistol; the ending's death shield sends no damage
// notify, and his health is full again 0.3 s later)
betrayal_hits()
{
	self endon( "death" );
	flag_wait( "made_it_to_lz" );
	self.mw2coop_qa_low = self.health;
	while ( !flag( "end_the_mission" ) )
	{
		self.mw2coop_qa_low = min( self.mw2coop_qa_low, self.health );
		wait 0.05;
	}
}

// Range starts (trainer.gsc firing_range_init() -> maps\_mw2coop_trainer::range_places): both
// players inside the sandbag bay, not stuck.
bay()
{
	check( level.players.size == 2 && isdefined( level.player2 ), "two players (" + level.players.size + ")" );
	level.mw2coop_no_follow = true;
	wait_intro();
	start = GetEnt( "player_start_range", "targetname" );
	foreach ( player in level.players )
	{
		check( qa_in_bay( player, start ), player_name( player ) + " is inside the sandbags (" + bay_text( player, start ) + ")" );
		check( !qa_stuck( player ), player_name( player ) + " is not stuck in anything solid" );
	}
	report( "done" );
}

// The obstacle course: the entrance gate closes once both players are upstairs, not when the
// first one gets there (the other was locked out or in).
course()
{
	check( level.players.size == 2 && isdefined( level.player2 ), "two players (" + level.players.size + ")" );
	level.mw2coop_no_follow = true;
	wait_intro();
	names = [];
	names[ 0 ] = "player_course_upstairs";
	names[ 1 ] = "player_course_end";
	foreach ( name in names )
		check( maps\_mw2coop_trainer::course_triggers( name ).size > 0, "a trigger sets " + name + " (" + maps\_mw2coop_trainer::course_triggers( name ).size + ")" );
	up = maps\_mw2coop_trainer::course_triggers( "player_course_upstairs" )[ 0 ];
	if ( !isdefined( up ) )
	{
		report( "done" );
		return;
	}
	// the course loop: start, then the stretch before the stairs
	flags = [];
	flags[ 0 ] = "player_has_started_course";
	flags[ 1 ] = "player_course_03a";
	flags[ 2 ] = "player_course_stairs2";
	foreach ( name in flags )
	{
		flag_set( name );
		wait 0.5;
	}
	gate = level.gate_cqb_enter;
	yaw = gate.angles[ 1 ];
	level.player SetOrigin( up.origin );
	wait 3;
	report( "player 1 upstairs: flag " + flag( "player_course_upstairs" ) + ", gate turned " + int( gate.angles[ 1 ] - yaw ) );
	check( flag( "player_course_upstairs" ), "player 1 set the upstairs flag" );
	check( Abs( gate.angles[ 1 ] - yaw ) < 1, "the entrance gate stays open while player 2 is behind" );
	level.player2 SetOrigin( up.origin + ( 30, 0, 0 ) );
	wait 3;
	report( "player 2 upstairs: gate turned " + int( gate.angles[ 1 ] - yaw ) );
	check( Abs( gate.angles[ 1 ] - yaw ) > 45, "the entrance gate closes once both are upstairs" );
	report( "done" );
}

// Inside the range's sandbag bay: between its ends (101 units left to 28 right of
// player_start_range), the bags close in front and the back wall close behind. Measured with
// player traces, 2026-10-22: from player 1's place the bags are 37 units ahead and the wall 7
// behind; outside, one of the two is open for 120 units.
qa_in_bay( player, start )
{
	right = AnglesToRight( ( 0, start.angles[ 1 ], 0 ) );
	fwd = AnglesToForward( ( 0, start.angles[ 1 ], 0 ) );
	side = VectorDot( player.origin - start.origin, right );
	at = player.origin + ( 0, 0, 2 );
	front = Distance( at, PlayerPhysicsTrace( at, at + fwd * 120 ) );
	back = Distance( at, PlayerPhysicsTrace( at, at - fwd * 120 ) );
	return side > -101 && side < 28 && front < 60 && back < 40;
}

bay_text( player, start )
{
	right = AnglesToRight( ( 0, start.angles[ 1 ], 0 ) );
	fwd = AnglesToForward( ( 0, start.angles[ 1 ], 0 ) );
	at = player.origin + ( 0, 0, 2 );
	return int( VectorDot( player.origin - start.origin, right ) ) + " right of the start, bags " + int( Distance( at, PlayerPhysicsTrace( at, at + fwd * 120 ) ) ) + " ahead, wall " + int( Distance( at, PlayerPhysicsTrace( at, at - fwd * 120 ) ) ) + " behind";
}

// The box (trainer.gsc spawn_frags()): its trigger is a touch trigger and the level checks that
// he looks at the box, so each player walks up to it (real Forward presses) and looks at it.
pickup_frags()
{
	flag_wait( "frags_have_been_spawned" );
	box = GetEnt( "grenade_box", "targetname" );
	trigger = GetEnt( "frag_trigger", "script_noteworthy" );
	foreach ( n, player in level.players )
	{
		from = int( Distance2D( player.origin, box.origin ) );
		for ( i = 0; i < 16 && !player IsTouching( trigger ); i++ )
		{
			player SetPlayerAngles( ( 0, VectorToAngles( box.origin - player.origin )[ 1 ], 0 ) );
			press( player, "forward", 250 );
			wait 0.2;
		}
		report( player_name( player ) + " walked up to the box: " + i + " Forward presses, from " + from + " to " + int( Distance2D( player.origin, box.origin ) ) + " units from it" );
		for ( i = 0; i < 10 && player GetWeaponAmmoStock( "fraggrenade" ) < 3; i++ )
		{
			player SetPlayerAngles( VectorToAngles( box.origin - player GetEye() ) );
			wait 0.5;
		}
		check( player HasWeapon( "fraggrenade" ) && player GetWeaponAmmoStock( "fraggrenade" ) >= 3, "player " + ( n + 1 ) + " takes frags from the box (" + player GetWeaponAmmoStock( "fraggrenade" ) + ")" );
		// and steps back out of the other player's way
		player SetPlayerAngles( ( 0, VectorToAngles( box.origin - player.origin )[ 1 ], 0 ) );
		for ( i = 0; i < 4; i++ )
			press( player, "back", 250 );
	}
	report( "done" );
}

qa_stand_by( player, at, dist )
{
	if ( !isdefined( dist ) )
		dist = 40;
	away = ( player.origin[ 0 ] - at[ 0 ], player.origin[ 1 ] - at[ 1 ], 0 );
	if ( Length( away ) < 1 )
		away = ( 1, 0, 0 );
	// on the floor below that spot (a weapon lies on a table, the player may start on another level)
	spot = at + VectorNormalize( away ) * dist;
	player SetOrigin( PlayerPhysicsTrace( spot + ( 0, 0, 16 ), spot - ( 0, 0, 200 ) ) );
	wait 0.1;
}

// A table pickup by hand: he stands by the weapon, looks at it and presses Use (up to 4 tries);
// then he walks back to where he stood.
qa_use_take( player, item, weapon )
{
	back = player.origin;
	qa_stand_at_table( player, item.origin );
	for ( i = 0; i < 4 && !player HasWeapon( weapon ); i++ )
	{
		if ( isdefined( item ) )
			player SetPlayerAngles( VectorToAngles( item.origin - player GetEye() ) );
		wait 0.2;
		press( player, "activate", 500 );
		for ( t = 0; t < 2 && !player HasWeapon( weapon ); t += 0.25 )
			wait 0.25;
		if ( !player HasWeapon( weapon ) && isdefined( item ) )
			report( player_name( player ) + ": no " + weapon + " after Use " + ( i + 1 ) + " (" + int( Distance( player GetEye(), item.origin ) ) + " units from the eye to it; holds " + weapons_text( player ) + ")" );
	}
	report( player_name( player ) + " pressed Use " + i + " times for the " + weapon );
	player SetOrigin( back );
	wait 0.5;
}

// Where a player would stand to take something off a table: 40 units from it on the floor (not on
// the sandbags beside it), free all round, clear of the other player, with a clear view of it.
// Tried from his own side first.
qa_stand_at_table( player, at )
{
	other = level.player;
	if ( player == level.player )
		other = level.player2;
	yaw = VectorToAngles( ( player.origin[ 0 ] - at[ 0 ], player.origin[ 1 ] - at[ 1 ], 0 ) )[ 1 ];
	turns = strtok( "0 45 -45 90 -90 135 -135 180", " " );
	grounds = [];
	floor_z = undefined;
	foreach ( turn in turns )
	{
		spot = at + AnglesToForward( ( 0, yaw + int( turn ), 0 ) ) * 40;
		grounds[ turn ] = PlayerPhysicsTrace( spot + ( 0, 0, 16 ), spot - ( 0, 0, 200 ) );
		if ( !isdefined( floor_z ) || grounds[ turn ][ 2 ] < floor_z )
			floor_z = grounds[ turn ][ 2 ];
	}
	foreach ( turn in turns )
	{
		ground = grounds[ turn ];
		if ( ground[ 2 ] > floor_z + 8 || qa_spot_stuck( ground ) || Distance2D( ground, other.origin ) < 48 )
			continue;
		if ( !BulletTracePassed( ground + ( 0, 0, 60 ), at, false, undefined ) )
			continue;
		player SetOrigin( ground );
		wait 0.1;
		return;
	}
	report( player_name( player ) + ": no free floor by the table, standing by it anyway" );
	qa_stand_by( player, at );
}

// The level's intro (maps\_introscreen) holds the players until its title has gone. Most intros
// set the flag introscreen_complete at the end, some only notify it (dc_whitehouse); none takes
// 15 s.
wait_intro()
{
	if ( !flag_exist( "introscreen_complete" ) || !flag( "introscreen_complete" ) )
		level waittill_notify_or_timeout( "introscreen_complete", 15 );
	wait 0.5;
}

// True when no 8-unit step is free in any direction: he stands inside something solid.
qa_stuck( player )
{
	return qa_spot_stuck( player.origin );
}

qa_spot_stuck( at )
{
	foreach ( yaw in strtok( "0 90 180 270", " " ) )
	{
		end = at + AnglesToForward( ( 0, int( yaw ), 0 ) ) * 8 + ( 0, 0, 2 );
		if ( Distance( PlayerPhysicsTrace( at + ( 0, 0, 2 ), end ), end ) < 0.5 )
			return false;
	}
	return true;
}

player_name( player )
{
	if ( player == level.player2 )
		return "player 2";
	return "player 1";
}

// Real input for a bot: the machine of that player presses the button as a key bound to it would
// ("activate" for +activate, "forward", "gostand", ...) for ms milliseconds, in steps of 250 ms
// (mw2coop_bind in the DLL, src/control.c). Both machines run this; only his acts on it. The DLL
// reads the request every 250 ms, hence the wait.
press( player, button, ms )
{
	if ( !isdefined( level.mw2coop_qa_presses ) )
		level.mw2coop_qa_presses = 0;
	level.mw2coop_qa_presses++;
	n = 1;
	if ( player == level.player2 )
		n = 2;
	SetDvar( "mw2coop_qa_press", n + " " + button + " " + ms + " " + level.mw2coop_qa_presses );
	wait 0.3;
}

// --------------------------------------------------------------------------------------------
// Real input (any map): each player's own machine presses Forward, then Use (press()). He walks
// forward, and the server sees his Use button held, not the other player's.
// split screen (settings.ini [splitscreen], mw2coopakepad.txt): player 2's copy is driven by a pad (a QA pad file holds the
// stick forward and turned); he walks and his view turns, player 1, driven by nobody, stays
padwalk()
{
	check( level.players.size == 2 && isdefined( level.player2 ), "two players (" + level.players.size + ")" );
	level.mw2coop_no_follow = true;
	wait_intro();
	wait 6;
	from = [];
	yaw = [];
	foreach ( n, player in level.players )
	{
		from[ n ] = player.origin;
		yaw[ n ] = player GetPlayerAngles()[ 1 ];
	}
	wait 5;
	foreach ( n, player in level.players )
	{
		moved = Distance2D( player.origin, from[ n ] );
		turned = AngleClamp180( player GetPlayerAngles()[ 1 ] - yaw[ n ] );
		report( "padwalk: " + player_name( player ) + " moved " + int( moved ) + ", turned " + int( turned ) );
	}
	// control: a Forward press through the game's own binding moves him, whoever drives the copy
	foreach ( n, player in level.players )
	{
		from[ n ] = player.origin;
		press( player, "forward", 1000 );
	}
	wait 2;
	foreach ( n, player in level.players )
		report( "padwalk control: " + player_name( player ) + " moved " + int( Distance2D( player.origin, from[ n ] ) ) );
	wait 1;
	report( "done" );
}

buttons()
{
	check( level.players.size == 2 && isdefined( level.player2 ), "two players (" + level.players.size + ")" );
	level.mw2coop_no_follow = true;
	wait_intro();
	wait 2;
	foreach ( n, player in level.players )
	{
		other = level.players[ 1 - n ];
		check( !qa_stuck( player ), player_name( player ) + " is not stuck in anything solid (at " + vec_int( player.origin ) + ")" );
		// face the way with the most free floor (up to 100 units; the range's bays are narrow)
		yaw = player GetPlayerAngles()[ 1 ];
		room = 0;
		best = yaw;
		for ( turn = 0; turn < 360; turn += 45 )
		{
			from = player.origin + ( 0, 0, 8 );
			end = from + AnglesToForward( ( 0, yaw + turn, 0 ) ) * 100;
			free = Distance( from, PlayerPhysicsTrace( from, end ) );
			if ( free > room + 1 )
			{
				room = free;
				best = yaw + turn;
			}
		}
		yaw = best;
		need = min( 40, room * 0.5 );
		player SetPlayerAngles( ( 0, yaw, 0 ) );
		wait 0.2;
		// a level may hold a player still for a while: try again a few times
		moved = 0;
		other_moved = 0;
		for ( try = 1; try <= 5 && moved <= need; try++ )
		{
			if ( try > 1 )
				wait 3;
			from = player.origin;
			other_from = other.origin;
			press( player, "forward", 1000 );
			wait 1.5;
			moved = Distance2D( player.origin, from );
			other_moved = Distance2D( other.origin, other_from );
		}
		check( moved > need, player_name( player ) + " walks forward on a Forward press (" + int( moved ) + " of " + int( room ) + " free units, press " + ( try - 1 ) + ")" );
		check( other_moved < 8, "the other player stays (" + int( other_moved ) + " units)" );
		press( player, "activate", 1000 );
		held = false;
		other_held = false;
		for ( t = 0; t < 1.5 && !held; t += 0.05 )
		{
			held = player UseButtonPressed();
			other_held = other UseButtonPressed();
			wait 0.05;
		}
		check( held, "the server sees " + player_name( player ) + "'s Use button (" + t + " s)" );
		check( !other_held, "and not the other player's" );
		wait 1;
	}
	report( "done" );
}

vec_int( v )
{
	return int( v[ 0 ] ) + " " + int( v[ 1 ] ) + " " + int( v[ 2 ] );
}

// --------------------------------------------------------------------------------------------
// Who the enemies fight (any map, a start point with a fight): over 45 s, every 0.5 s, whose
// player each axis AI has as its enemy, and the hits each player takes from axis AI.
targets()
{
	check( level.players.size == 2 && isdefined( level.player2 ), "two players (" + level.players.size + ")" );
	foreach ( player in level.players )
	{
		player.mw2coop_qa_hits = 0;
		player thread count_hits();
	}
	on1 = 0;
	on2 = 0;
	other = 0;
	samples = 0;
	most = 0;
	for ( t = 0; t < 45; t += 0.5 )
	{
		ai = GetAIArray( "axis" );
		most = max( most, ai.size );
		foreach ( guy in ai )
		{
			if ( !isdefined( guy.enemy ) )
				continue;
			samples++;
			if ( guy.enemy == level.player )
				on1++;
			else if ( guy.enemy == level.player2 )
				on2++;
			else
				other++;
		}
		wait 0.5;
	}
	report( "enemy samples " + samples + " (up to " + most + " axis AI): on player 1 " + on1 + ", on player 2 " + on2 + ", on allies " + other );
	report( "hits from axis AI: player 1 " + level.player.mw2coop_qa_hits + ", player 2 " + level.player2.mw2coop_qa_hits + "; players " + int( Distance( level.player.origin, level.player2.origin ) ) + " apart" );
	check( on2 > 0, "enemies take player 2 as their enemy too" );
	check( level.player2.mw2coop_qa_hits > 0 || on2 == 0, "and hit him" );
	report( "done" );
}

count_hits()
{
	self endon( "death" );
	for ( ;; )
	{
		self waittill( "damage", amount, attacker );
		if ( isdefined( attacker ) && IsAI( attacker ) && attacker.team == "axis" )
			self.mw2coop_qa_hits++;
	}
}

// --------------------------------------------------------------------------------------------
// Bodies check (any map): the players stand 120 units apart facing each other for 20 s, for
// screenshots of each one's body on the other's screen.
look()
{
	check( level.players.size == 2 && isdefined( level.player2 ), "two players (" + level.players.size + ")" );
	level.mw2coop_no_follow = true;
	wait 3;
	fwd = AnglesToForward( ( 0, level.player GetPlayerAngles()[ 1 ], 0 ) );
	spot = PlayerPhysicsTrace( level.player.origin + fwd * 120 + ( 0, 0, 48 ), level.player.origin + fwd * 120 - ( 0, 0, 96 ) );
	level.player2 SetOrigin( spot );
	// each machine saves its frame of the other player (src/shot.c): standing, then player 2
	// walking towards player 1 on a real Forward press (his body's walk animation)
	for ( i = 0; i < 200; i++ )
	{
		level.player SetPlayerAngles( VectorToAngles( level.player2 GetEye() - ( 0, 0, 20 ) - level.player GetEye() ) );
		level.player2 SetPlayerAngles( VectorToAngles( level.player GetEye() - ( 0, 0, 20 ) - level.player2 GetEye() ) );
		if ( i == 20 )
			report( "facing each other: player 2 model " + level.player2.model + ", player 1 model " + level.player.model );
		if ( i == 30 )
			thread qa_shot( "look-standing" );
		if ( i == 60 )
			thread press( level.player2, "forward", 1000 );
		if ( i == 68 )
			thread qa_shot( "look-walking" );
		wait 0.1;
	}
	report( "done" );
}

// No Russian's cover (airport, start "stairs"): player 2's shots at Makarov's squad blow it (the
// level only checks IsPlayer( attacker )); Makarov then shoots both and the mission fails for
// both (maps\_mw2coop_airport::makarov_kills_players). Names no airport script.
cover()
{
	check( level.players.size == 2 && isdefined( level.player2 ), "two players (" + level.players.size + ")" );
	for ( t = 0; t < 30 && !isdefined( level.makarov ); t += 0.5 )
		wait 0.5;
	check( isdefined( level.makarov ), "Makarov there" );
	wait 3;
	for ( i = 0; i < 6 && !flag( "friendly_fire_warning" ); i++ )
	{
		level.makarov DoDamage( 1, level.makarov.origin, level.player2 );
		wait 0.5;
	}
	check( flag( "friendly_fire_warning" ), "player 2's shots at Makarov's squad blew the cover (" + i + " hits)" );
	SetDvar( "mw2coop_qa_phase", "done" );	// the failure restarts the level
	for ( t = 0; t < 40 && !level.mw2coop.failing; t += 0.5 )
		wait 0.5;
	check( level.mw2coop.failing, "Makarov's squad turned on them and the mission failed for both (" + t + " s)" );
	report( "done" );
}

// Each machine saves frames of the other player's whole figure: standing, player 2 stepping to
// his right on a real press, backing off (src/shot.c). For body models (No Russian's transplant).
bodyview()
{
	check( level.players.size == 2 && isdefined( level.player2 ), "two players (" + level.players.size + ")" );
	level.mw2coop_no_follow = true;
	wait 3;
	fwd = AnglesToForward( ( 0, level.player GetPlayerAngles()[ 1 ], 0 ) );
	spot = PlayerPhysicsTrace( level.player.origin + fwd * 220 + ( 0, 0, 48 ), level.player.origin + fwd * 220 - ( 0, 0, 96 ) );
	level.player2 SetOrigin( spot );
	steps = [];
	steps[ 15 ] = "stand";
	steps[ 25 ] = "+moveright";
	steps[ 33 ] = "right";
	steps[ 45 ] = "+back";
	steps[ 53 ] = "back";
	for ( i = 0; i < 80; i++ )
	{
		level.player SetPlayerAngles( VectorToAngles( level.player2.origin + ( 0, 0, 36 ) - level.player GetEye() ) );
		level.player2 SetPlayerAngles( VectorToAngles( level.player.origin + ( 0, 0, 36 ) - level.player2 GetEye() ) );
		if ( isdefined( steps[ i ] ) )
		{
			if ( GetSubStr( steps[ i ], 0, 1 ) == "+" )
				thread press( level.player2, GetSubStr( steps[ i ], 1 ), 1500 );
			else
				thread qa_shot( "body-" + steps[ i ] );
		}
		wait 0.1;
	}
	report( "done" );
}

// No Russian's lobby (airport, start "intro"; run with MW2COOP_GOD=0): the squad kills whoever
// walks into its line of fire before it moves on (maps\_mw2coop_airport::line_of_fire_kill); the
// level used to kill player 1 whoever stepped in. Names no airport script.
lineoffire()
{
	check( level.players.size == 2 && isdefined( level.player2 ), "two players (" + level.players.size + ")" );
	for ( t = 0; t < 60 && !flag( "lobby_open_fire" ); t += 0.5 )
		wait 0.5;
	check( flag( "lobby_open_fire" ), "the squad opens fire in the lobby (" + t + " s)" );
	trig = GetEnt( "intro_line_of_fire_trig_final", "targetname" );
	check( isdefined( trig ) && !flag( "lobby_to_stairs_go" ), "the line of fire is live" );
	SetDvar( "mw2coop_qa_phase", "done" );	// the death restarts the level
	level.player2 SetOrigin( trig.origin );
	for ( t = 0; t < 5 && IsAlive( level.player2 ) && !level.mw2coop.failing; t += 0.1 )
		wait 0.1;
	check( !IsAlive( level.player2 ) || level.mw2coop.failing, "player 2 died in the line of fire (" + t + " s)" );
	check( IsAlive( level.player ), "player 1 still alive" );
	check( isdefined( level.mw2coop_lof_victim ) && level.mw2coop_lof_victim == level.player2, "the squad's line of fire killed player 2, the one who walked in" );
	for ( t = 0; t < 10 && !level.mw2coop.failing; t += 0.5 )
		wait 0.5;
	check( level.mw2coop.failing, "the mission failed for both" );
	report( "done" );
}

// Aim assist of player 2's pad (see the list at the top). Each phase's samples go out in one
// report: report() waits, and the DLL reads the log dvar every 250 ms.
aimassist()
{
	check( level.players.size == 2 && isdefined( level.player2 ), "two players (" + level.players.size + ")" );
	level.mw2coop_no_follow = true;
	if ( !flag_exist( "introscreen_complete" ) || !flag( "introscreen_complete" ) )
		level waittill_notify_or_timeout( "introscreen_complete", 30 );
	wait 8;	// the title lines are gone
	p2 = level.player2;
	// the allies are drawn (Price was not, with the aim target check writing every entity's flags)
	allies = GetAIArray( "allies" );
	text = allies.size + " allies:";
	foreach ( ally in allies )
		text = text + " " + ally GetEntityNumber() + " " + ally.model + " " + int( Distance( ally.origin, level.player.origin ) );
	report( text );
	if ( allies.size )
	{
		ally = allies[ 0 ];
		foreach ( other in allies )
			if ( DistanceSquared( other.origin, level.player.origin ) < DistanceSquared( ally.origin, level.player.origin ) )
				ally = other;
		for ( t = 0; t < 10; t++ )
		{
			foreach ( player in level.players )
				player SetPlayerAngles( VectorToAngles( ally GetEye() - player GetEye() ) );
			wait 0.05;
		}
		SetDvar( "mw2coop_qa_frame", "aa-ally" );
		wait 0.5;
	}
	guy = aa_enemy( p2 );
	check( isdefined( guy ), "an enemy to aim at" );
	if ( !isdefined( guy ) )
		return;
	foreach ( player in level.players )
		player.ignoreme = true;
	guy.ignoreall = true;
	guy.goalradius = 8;
	yaw = p2 GetPlayerAngles()[ 1 ];
	ahead = p2.origin + AnglesToForward( ( 0, yaw, 0 ) ) * 400;
	spot = PlayerPhysicsTrace( ahead + ( 0, 0, 48 ), ahead - ( 0, 0, 96 ) );
	guy ForceTeleport( spot, ( 0, yaw + 180, 0 ) );
	guy SetGoalPos( spot );
	guy thread aa_hold( spot, yaw + 180 );
	start = p2.origin;
	wait 1;
	report( "enemy " + guy GetEntityNumber() + " " + int( Distance( p2.origin, guy.origin ) ) + " units ahead" );

	// 1: aiming down the sight 6 degrees beside it
	aa_aim( p2, guy, 6 );
	wait 0.5;
	text = "snap: before " + aa_off( p2, guy );
	SetDvar( "mw2coop_qa_pad2", "0 0 0 0 0 255 0" );
	for ( i = 1; i <= 8; i++ )
	{
		wait 0.1;
		text = text + ", " + ( i * 100 ) + " ms " + aa_off( p2, guy );
		if ( i == 6 )
			SetDvar( "mw2coop_qa_frame", "aa-ads" );
	}
	off = aa_yaw_off( p2, guy );
	report( text );
	check( abs( off ) < 2, "aiming down the sight turned player 2 onto the enemy (" + off + " degrees off)" );
	SetDvar( "mw2coop_qa_pad2", "0 0 0 0 0 0 0" );
	wait 1;

	// 2: a steady turn to the right from 25 degrees to the left of it
	aa_aim( p2, guy, 25 );
	wait 0.5;
	SetDvar( "mw2coop_qa_pad2", "0 0 16000 0 0 0 0" );
	text = "turn (off, degrees a second):";
	far = 0;
	over = 0;
	far_n = 0;
	over_n = 0;
	last = p2 GetPlayerAngles()[ 1 ];
	for ( i = 0; i < 24; i++ )
	{
		wait 0.05;
		now = p2 GetPlayerAngles()[ 1 ];
		speed = AngleClamp180( last - now ) / 0.05;
		last = now;
		off = aa_yaw_off( p2, guy );
		text = text + " " + int( off ) + "/" + int( speed );
		if ( abs( off ) < 2 )
		{
			over += speed;
			over_n++;
		}
		else if ( abs( off ) > 10 )
		{
			far += speed;
			far_n++;
		}
	}
	SetDvar( "mw2coop_qa_pad2", "0 0 0 0 0 0 0" );
	report( text );
	if ( far_n && over_n )
		check( over / over_n < 0.8 * far / far_n, "the turn slows over the enemy (" + int( far / far_n ) + " away, " + int( over / over_n ) + " over it)" );
	else
		report( "FAIL the turn did not pass over the enemy (" + far_n + " samples away, " + over_n + " over it)" );
	wait 1;

	// 3: strafing right for a second while looking at it, with aim assist and without (the pad
	// dvar's eighth value turns it off: a script cannot write the option's code dvar)
	result = [];
	for ( assist = 1; assist >= 0; assist-- )
	{
		p2 SetOrigin( start );
		aa_aim( p2, guy, 0 );
		wait 0.5;
		SetDvar( "mw2coop_qa_pad2", "32767 0 0 0 0 0 0 " + ( 1 - assist ) );
		wait 0.5;
		if ( assist )
			SetDvar( "mw2coop_qa_frame", "aa-strafe" );
		wait 0.5;
		result[ assist ] = aa_yaw_off( p2, guy );
		SetDvar( "mw2coop_qa_pad2", "0 0 0 0 0 0 0" );
		wait 0.5;
	}
	level notify( "mw2coop_aa_done" );
	check( abs( result[ 1 ] ) < abs( result[ 0 ] ) * 0.8, "a strafe keeps nearer the enemy with aim assist (" + result[ 1 ] + " degrees off, without " + result[ 0 ] + ")" );
	// 4: vibration ([native] rumblelog=1 logs it): damage on player 2, a grenade near him, one far off
	p2 PlayRumbleOnEntity( "damage_heavy" );
	wait 1;
	PlayRumbleOnPosition( "grenade_rumble", p2.origin + ( 200, 0, 0 ) );
	wait 1;
	PlayRumbleOnPosition( "grenade_rumble", p2.origin + ( 5000, 0, 0 ) );
	wait 1;
	report( "rumbles played: damage on player 2, a grenade 200 units off, one 5000 units off" );
	report( "done" );
}

// keeps an AI at one spot facing one way (a patrol would walk it off)
aa_hold( spot, yaw )
{
	self endon( "death" );
	level endon( "mw2coop_aa_done" );
	for ( ;; )
	{
		self ForceTeleport( spot, ( 0, yaw, 0 ) );
		wait 0.05;
	}
}

// the axis AI nearest p, or one spawned from an axis spawner
aa_enemy( p )
{
	best = undefined;
	foreach ( guy in GetAIArray( "axis" ) )
		if ( !isdefined( best ) || DistanceSquared( guy.origin, p.origin ) < DistanceSquared( best.origin, p.origin ) )
			best = guy;
	if ( isdefined( best ) )
		return best;
	foreach ( spawner in GetSpawnerTeamArray( "axis" ) )
	{
		guy = spawner StalingradSpawn();
		if ( !spawn_failed( guy ) )
			return guy;
	}
	return undefined;
}

aa_aim( p, guy, yaw_off )
{
	ang = VectorToAngles( guy GetTagOrigin( "j_head" ) - p GetEye() );
	p SetPlayerAngles( ( ang[ 0 ], ang[ 1 ] + yaw_off, 0 ) );
}

aa_yaw_off( p, guy )
{
	return AngleClamp180( p GetPlayerAngles()[ 1 ] - VectorToAngles( guy GetTagOrigin( "j_head" ) - p GetEye() )[ 1 ] );
}

aa_off( p, guy )
{
	ang = VectorToAngles( guy GetTagOrigin( "j_head" ) - p GetEye() );
	now = p GetPlayerAngles();
	return int( AngleClamp180( now[ 1 ] - ang[ 1 ] ) * 10 ) / 10 + "/" + int( AngleClamp180( now[ 0 ] - ang[ 0 ] ) * 10 ) / 10;
}

// Player 2's body faces where his camera looks (see the list at the top; the owner saw it turned
// away from the camera on Contingency, 2026-10-28)
bodycam()
{
	check( level.players.size == 2 && isdefined( level.player2 ), "two players (" + level.players.size + ")" );
	level.mw2coop_no_follow = true;
	if ( !flag_exist( "introscreen_complete" ) || !flag( "introscreen_complete" ) )
		level waittill_notify_or_timeout( "introscreen_complete", 30 );
	wait 2;
	p1 = level.player;
	p2 = level.player2;
	yaw = p1 GetPlayerAngles()[ 1 ];
	ahead = p1.origin + AnglesToForward( ( 0, yaw, 0 ) ) * 150;
	p2 SetOrigin( PlayerPhysicsTrace( ahead + ( 0, 0, 48 ), ahead - ( 0, 0, 96 ) ) );
	for ( i = 0; i < 4; i++ )
	{
		p2 SetPlayerAngles( ( 0, yaw + 180 + i * 90, 0 ) );	// 0: facing player 1
		for ( t = 0; t < 20; t++ )
		{
			p1 SetPlayerAngles( VectorToAngles( p2 GetEye() - ( 0, 0, 20 ) - p1 GetEye() ) );
			wait 0.05;
		}
		SetDvar( "mw2coop_qa_frame", "bodycam-" + ( i * 90 ) );
		report( "player 2 turned " + ( i * 90 ) + " from facing player 1: view yaw " + int( p2 GetPlayerAngles()[ 1 ] ) + ", body yaw " + int( p2.angles[ 1 ] ) + ", player 1 yaw " + int( yaw ) );
	}
	SetDvar( "mw2coop_qa_pad2", "0 0 20000 0 0 0 0" );
	for ( t = 0; t < 20; t++ )
	{
		p1 SetPlayerAngles( VectorToAngles( p2 GetEye() - ( 0, 0, 20 ) - p1 GetEye() ) );
		wait 0.05;
	}
	SetDvar( "mw2coop_qa_pad2", "0 0 0 0 0 0 0" );
	wait 0.5;
	SetDvar( "mw2coop_qa_frame", "bodycam-pad" );
	report( "player 2 turned by his pad: view yaw " + int( p2 GetPlayerAngles()[ 1 ] ) + ", body yaw " + int( p2.angles[ 1 ] ) );
	report( "done" );
}

// a hint naming a key for each player (see the list at the top)
glyphs()
{
	check( level.players.size == 2 && isdefined( level.player2 ), "two players (" + level.players.size + ")" );
	if ( !flag_exist( "introscreen_complete" ) || !flag( "introscreen_complete" ) )
		level waittill_notify_or_timeout( "introscreen_complete", 30 );
	wait 3;
	foreach ( player in level.players )
	{
		hint = NewClientHudElem( player );
		hint.x = 0;
		hint.y = 60;
		hint.alignX = "center";
		hint.alignY = "middle";
		hint.horzAlign = "center";
		hint.vertAlign = "middle";
		hint.fontScale = 1.6;
		hint.font = "default";
		hint SetText( &"TRAINER_HINT_STAND" );
	}
	wait 2;
	SetDvar( "mw2coop_qa_frame", "glyphs" );
	wait 1;
	report( "done" );
}

// both players armed and facing each other 150 units apart ([native] entdump=1 logs the entity
// slots: which fields carry a body's weapon); the composed picture is saved as "armed"
armed()
{
	check( level.players.size == 2 && isdefined( level.player2 ), "two players (" + level.players.size + ")" );
	if ( !flag_exist( "introscreen_complete" ) || !flag( "introscreen_complete" ) )
		level waittill_notify_or_timeout( "introscreen_complete", 30 );
	wait 2;
	foreach ( player in level.players )
		if ( player GetCurrentWeapon() == "none" )
		{
			player GiveWeapon( "m4_grunt" );
			player SwitchToWeapon( "m4_grunt" );
		}
	wait 1;
	p1 = level.player;
	p2 = level.player2;
	p1 thread armed_watch( "player 1" );
	p2 thread armed_watch( "player 2" );
	yaw = p1 GetPlayerAngles()[ 1 ];
	ahead = p1.origin + AnglesToForward( ( 0, yaw, 0 ) ) * 150;
	p2 SetOrigin( PlayerPhysicsTrace( ahead + ( 0, 0, 48 ), ahead - ( 0, 0, 96 ) ) );
	for ( t = 0; t < 160; t++ )
	{
		p1 SetPlayerAngles( VectorToAngles( p2 GetEye() - ( 0, 0, 20 ) - p1 GetEye() ) );
		p2 SetPlayerAngles( VectorToAngles( p1 GetEye() - ( 0, 0, 20 ) - p2 GetEye() ) );
		if ( t == 100 )
			SetDvar( "mw2coop_qa_frame", "armed" );
		wait 0.05;
	}
	report( "player 1 holds " + p1 GetCurrentWeapon() + ", player 2 " + p2 GetCurrentWeapon() );
	// player 2 changes to a pistol first: his body in player 1's half (the top one)
	p2 GiveWeapon( "usp" );
	p2 SwitchToWeapon( "usp" );
	for ( t = 0; t < 80; t++ )
	{
		p1 SetPlayerAngles( VectorToAngles( p2 GetEye() - ( 0, 0, 20 ) - p1 GetEye() ) );
		p2 SetPlayerAngles( VectorToAngles( p1 GetEye() - ( 0, 0, 20 ) - p2 GetEye() ) );
		if ( t == 60 )
			SetDvar( "mw2coop_qa_frame", "armed-p2usp" );
		wait 0.05;
	}
	report( "player 2 changed to " + p2 GetCurrentWeapon() );
	// player 1 changes to a pistol: his body in player 2's half must hold it (and the rifle again after)
	p1 GiveWeapon( "usp" );
	for ( round = 0; round < 2; round++ )
	{
		if ( round == 0 )
			p1 SwitchToWeapon( "usp" );
		else
			p1 SwitchToWeapon( level.motiontrackergun_off );
		for ( t = 0; t < 80; t++ )
		{
			p1 SetPlayerAngles( VectorToAngles( p2 GetEye() - ( 0, 0, 20 ) - p1 GetEye() ) );
			p2 SetPlayerAngles( VectorToAngles( p1 GetEye() - ( 0, 0, 20 ) - p2 GetEye() ) );
			if ( t == 60 )
				SetDvar( "mw2coop_qa_frame", "armed-switched" + round );
			wait 0.05;
		}
		text = "player 1 changed to " + p1 GetCurrentWeapon() + "; his attached models:";
		for ( i = 0; i < p1 GetAttachSize(); i++ )
			text = text + " " + p1 GetAttachModelName( i ) + "@" + p1 GetAttachTagName( i );
		report( text + " (model " + p1.model + ")" );
	}
	report( "done" );
}

armed_watch( who )
{
	for ( ;; )
	{
		self waittill( "weaponchange" );
		SetDvar( "mw2coop_qa_log", "weaponchange " + who + ": " + self GetCurrentWeapon() + ", model " + self GetPlayerWeaponModel() );
		wait 0.3;
	}
}
