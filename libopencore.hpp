/*
 * Copyright (c) 2007 cycad <cycad@zetasquad.com>
 *
 * This software is provided 'as-is', without any express or implied 
 * warranty. In no event will the author be held liable for any damages 
 * arising from the use of this software.
 *
 * Permission is granted to anyone to use this software for any purpose,
 * including commercial applications, and to alter it and redistribute
 * it freely, subject to the following restrictions:
 *
 *     1. The origin of this software must not be misrepresented; you
 *     must not claim that you wrote the original software. If you use 
 *     this software in a product, an acknowledgment in the product 
 *     documentation would be appreciated but is not required.
 *
 *     2. Altered source versions must be plainly marked as such, and
 *     must not be misrepresented as being the original software.
 *
 *     3. This notice may not be removed or altered from any source
 *     distribution.
 */

/*
 * opencore is essentially a reimplementation of Ekted's PowerBot.
 * 
 * The libs/ folder contains a test plugin, features.cpp.  This
 * demonstrates the the capabilities of the core and explains them.
 *
 * Both the core and the test plugin can each be compiled with
 * the Makefile in their directories. 'make' is all thats needed
 * to compile them.
 *
 * The file 'master.conf' is the main bot's configuration file. Change
 * ServerName in it to the IP of the test server. On the machine running
 * the test server open subbill.exe and then subgame2.exe in that order.
 *
 * opencore should now be able to connect. 
 */

/*
 * Bot Management
 *
 * This hasn't been designed yet so bot management is ugly. The
 * single configuration file for each bot will need to be changed.
 * Stopping/Starting bots is also ugly.
 */

/*
 * Player Management
 *
 * Every time a player enters or leaves an arena there are events
 * generated, EVENT_ENTER and EVENT_LEAVE.
 *
 * Player management such as player allocation and deallocation
 * is internal to the core. As such, pointers to PLAYER objects are
 * NOT guaranteed to be constant between different events. In
 * order to keep track of a specific player, store the player's 
 * PID and not the player's pointer.
 *
 * Player PID's are assigned by the subgame server and are
 * guaranteed by the core to remain constant between ENTER and
 * LEAVE events. The core only has visibility in one arena, however,
 * and can't guarantee that a player's PID will remain constant
 * if a player enters, leaves, and then reenters. Use a player's
 * name to keep track of players between LEAVE and ENTER events.
 *
 * Player-specific data is managed by the core using Player Info or
 * pinfo for short. pinfo is guaranteed to be constant even across
 * ENTER and LEAVE events pinfo could be used to keep track of how
 * many kills an individual player has had since the bot has loaded
 * into the arena, for example.
 *
 * When a player is first seen by the core an EVENT_FIRSTSEEN
 * is generated. pinfo is always zeroed out before EVENT_FIRSTSEEN.
 * Plugins can use this event to do dynamic allocations and
 * initialization of pinfo.
 *
 * To use pinfo, you need to define a structure and then set the
 * LIB_PINFO_SIZE to the size of the pinfo structure.
 *
 * A small pinfo example:
 *
 *	// global namespace
 *	struct PlayerInfo {
 *		int nkills;		// number of kills
 *		std::list<int> l;	// example dynamic data
 *	};
 *	LIB_PINFO_SIZE = sizeof(struct PlayerInfo);
 *
 *	// in EVENT_FIRSTSEEN
 *	struct PlayerInfo *pi = GetPlayerInfo(cd, data->p1);
 *	pi->nkills = 0;	// unnecessary since pinfo is zeroed
 *	pi->l = new std::list<int>;
 *
 *	// in EVENT_DEADSLOT
 *	struct PlayerInfo *pi = GetPlayerInfo(cd, data->p1);
 *	delete pi->l;
 *
 */

/*
 * Thread-Specific Data / Bot-Specific Data
 *
 * Any number of threads can be active in a library at any one time,
 * therefore access to any writable global data must be synchronized.
 * 
 * To have data that is specific to a single thread (bot) you must
 * use the 'user_data' convention.  CORE_DATA.user_data is a
 * thread-specific pointer to memory region where a thread can
 * store values it needs. CORE_DATA.user_data should be allocated
 * and set during EVENT_START and freed during EVENT_STOP.
 * The user_data structure allocated should contain all the thread-
 * specific variables that a thread needs.
 *
 * An example of keeping track of the total number of kills that
 * have occurred in an arena since the bot joined using a thread-
 * specific approach.
 *
 *	// global namespace
 *	struct UserData {
 *		int tkills;	// total kills in the arena
 * 	};
 *
 *	// in EVENT_START
 *	cd->user_data = malloc(sizeof(struct UserData));
 *
 *	// in EVENT_KILL
 *	struct UserData *ud = (struct UserData)cd->user_data;
 *	ud->tkills += 1;
 *
 *	// in EVENT_STOP
 *	free(ud->user_data);
 *
 * With this example each thread will have its own unique user_data
 * pointer and total kill counter. If this plugin is loaded by
 * bots in 3 different arenas they will each keep their own kill
 * tally.
 *
 * And a global version:
 *
 *	// global namespace
 *	static int g_tkills = 0;
 *	static pthread_mutex_t g_tkills_mutex =
 *	    PTHREAD_MUTEX_INITIALIZER;
 *
 *	// in EVENT_KILL
 *	pthread_mutex_lock(&g_tkills_mutex);
 *	g_tkills++;
 *	pthread_mutex_lock(&g_tkills_mutex);
 *
 * If multiple bots were loaded that used this plugin they
 * would all share the same kill counter.  This would keep track
 * of kills in all arenas that plugin is running in.
 */

/*
 * Operators and Access Levels
 *
 * All players have an access level ranging from 0-9. Players not listed
 * in ops.conf are level 0. Players in ops.conf have the access level
 * specified on their line.
 */

#ifndef _LIBOPENCORE_HPP
#define _LIBOPENCORE_HPP

#include <stdint.h>
#include <stdlib.h>

/* C linkage for functions */
extern "C" {

/* This must match the core's version string */	
#define CORE_VERSION "0.18"

typedef uint32_t ticks_ms_t;
typedef uint32_t ticks_hs_t;

/*
 * These are the events that the core exports to modules.  In each event
 * only SOME of the variables in the CORE_DATA structure are set. Any
 * elements that are not set by the current event being handled should
 * be treated as INVALID during that event.
 *
 * Variables set in CORE_DATA during each event are noted next to
 * the event name.
 *
 * Variables that are sometimes set have a '!' suffix.
 */
				/* CORE_DATA elements set: */
/*
 * EVENT_START occurs when the library is loaded. It
 * can be used to allocate CORE_DATA.user_data, register
 * commands, and do other library-specific setup operations.
 */
#define	EVENT_START	0	/* none */

/*
 * EVENT_STOP occurs when the module is unloaded. It can be used
 * to free CORE_DATA.user_data and to perform other shutdown
 * operations.
 */
#define	EVENT_STOP	1	/* none */

/*
 * EVENT_LOGIN occurs when the bot logs in to the server.
 * For modules loaded when the bot is already connected
 * this event does not occur.
 */
#define EVENT_LOGIN	2	/* none */

/*
 * EVENT_KILL occurs when a player is killed.
 *
 * p1 is killed by p2.
 */
#define EVENT_KILL	3	/* p1, p2 */
	
/*
 * EVENT_FIRSTSEEN occurs when a player is first seen
 * by the bot. This can be used to do allocations and 
 * initializations in pinfo.
 *
 * Players are kept cached for 1 hour since they were last seen.
 * After a player has not been seen for 1 hour their internal
 * player entry is deleted and an EVENT_DEADSLOT is generated.
 * This event should be used to free any dynamically allocated
 * pinfo data during EVENT_FIRSTSEEN.
 */
#define EVENT_FIRSTSEEN	4	/* p1 */

/*
 * EVENT_ENTER occurs when a player enters the arena.
 */
#define EVENT_ENTER	5	/* p1 */

/*
 * EVENT_LEAVE occurs when a player leaves the arena.
 * When a bot disconnects or shuts down this event does
 *  not occur.
 */
#define EVENT_LEAVE	6	/* p1 */

/*
 * EVENT_CHANGE occurs when a player changes ship.
 */
#define EVENT_CHANGE	7	/* p1, old_freq, old_ship */

/*
 * EVENT_DEADSLOT occurs when a player is deleted from
 * the core-managed player array. This happens 1 hour
 * since a player was last seen.  This event can be used
 * to deallocate anything in pinfo. This is guaranteed
 * to occur.
 */
#define EVENT_DEADSLOT	8	/* p1 */

/*
 * EVENT_MESSAGE occurs when the bot receives a message.
 *
 * p1 always set, but is sometimes NULL such as during
 * arena or remote messages.
 */
#define EVENT_MESSAGE	9	/* msg, msg_{type,name,chatname!,chatid!}, p1! */

/*
 * EVENT_ALERT occurs when ?alerts are sent.
 */
#define EVENT_ALERT	10	/* alert_{msg,name,type,arena} */

/*
 * EVENT_UPDATE occurs when a position packet is received.
 */
#define EVENT_UPDATE	11	/* p1, p1.status, p1.x, p1.y */

/*
 * EVENT_TICK occurs every 100ms. It can be used for periodic
 * checks.
 */
#define EVENT_TICK	12	/* none */

/*
 * EVENT_TIMER occurs when a timer set by SetTimer() expires.
 * timer_id is the timer's unique id number and can be used
 * to identify a specific timer. timer_data1 and timer_data2
 * are set exactly as they were passed to SetTimer().
 *
 * Timers are only granular to 100ms.
 */
#define EVENT_TIMER	13	/* timer_id, timer_data1, timer_data2 */

/*
 * This event is not exported to the event callback, commands
 * go directly to their handling callbacks.
 */
#define EVENT_COMMAND	14	/* cmd_* */

/*
 * Happens when a players *info data is updated.
 */
#define EVENT_INFO	15	/* p1, p1->info */

/*
 * Happens when a players *einfo data is updated.
 */
#define EVENT_EINFO	16	/* p1, p1->einfo */

/*
 * Happens when the bot gets disconnected.
 */
#define EVENT_DISCONNECT 17	/* none */

/*
 * Happens when a file has been received.
 */
#define EVENT_TRANSFER	18	/* transfer_direction, transfer_filename, transfer_success */

/*
 * Happens when a file has been received.
 */
#define EVENT_ARENA_CHANGE	19	/* bot_arena */

/*
 * Happens when the server is showing clients the arena list.
 */
#define EVENT_ARENA_LIST	20	/* arena_list, arena_list_count */

typedef uint16_t FREQ;
#define FREQ_NONE		(FREQ)0xFFFF

typedef uint8_t MSG_TYPE;
#define MSG_ARENA		(MSG_TYPE)0x00
#define MSG_PUBLIC_MACRO	(MSG_TYPE)0x01
#define MSG_PUBLIC		(MSG_TYPE)0x02
#define MSG_TEAM		(MSG_TYPE)0x03
#define MSG_FREQ		(MSG_TYPE)0x04
#define MSG_PRIVATE		(MSG_TYPE)0x05
#define MSG_WARNING		(MSG_TYPE)0x06
#define MSG_REMOTE		(MSG_TYPE)0x07
#define MSG_SYSOP		(MSG_TYPE)0x08
#define MSG_CHAT		(MSG_TYPE)0x09

#define STATUS_STEALTH	(uint8_t)0x01
#define STATUS_CLOAK	(uint8_t)0x02
#define STATUS_XRADAR	(uint8_t)0x04
#define STATUS_ANTIWARP	(uint8_t)0x08
#define STATUS_UNCLOAK	(uint8_t)0x10
#define STATUS_SAFE	(uint8_t)0x20
#define STATUS_UNKNOWN	(uint8_t)0x40
#define STATUS_CONSTANT_VELOCITY (uint8_t)0x80

typedef uint8_t CMD_TYPE;
#define CMD_REMOTE	(CMD_TYPE)0x01
#define CMD_PUBLIC	(CMD_TYPE)0x02
#define CMD_PRIVATE	(CMD_TYPE)0x04
#define CMD_CHAT	(CMD_TYPE)0x08

typedef uint16_t PLAYER_ID;
#define PID_NONE	(PLAYER_ID)0xFFFF

typedef uint8_t SHIP;
#define SHIP_NONE	SHIP_SPECTATOR
#define SHIP_WARBIRD	(SHIP)0x00
#define SHIP_JAVELIN	(SHIP)0x01
#define SHIP_SPIDER	(SHIP)0x02
#define SHIP_LEVIATHAN	(SHIP)0x03
#define SHIP_TERRIER	(SHIP)0x04
#define SHIP_WEASEL	(SHIP)0x05
#define SHIP_LANCASTER	(SHIP)0x06
#define SHIP_SHARK	(SHIP)0x07
#define SHIP_SPECTATOR  (SHIP)0x08

typedef uint8_t MATCH_TYPE;
#define MATCH_HERE	(MATCH_TYPE)0x01	/* Match players present in arena */
#define MATCH_GONE	(MATCH_TYPE)0x02	/* Match players absent from arena*/
#define MATCH_PREFIX	(MATCH_TYPE)0x04	/* Match name prefix (name search only) */

typedef uint8_t SOUND;
#define SOUND_NONE	(SOUND)0x00

typedef uint8_t OP_LEVEL;
#define OP_PLAYER	(OP_LEVEL)0x00
#define OP_REF		(OP_LEVEL)0x01
#define OP_MOD		(OP_LEVEL)0x02
#define OP_SENMOD	(OP_LEVEL)0x03
#define OP_ASMOD	(OP_LEVEL)0x04
#define OP_SMOD		(OP_LEVEL)0x05
#define OP_SENSMOD	(OP_LEVEL)0x06
#define OP_HSMOD	(OP_LEVEL)0x07
#define OP_SYSOP	(OP_LEVEL)0x08
#define OP_OWNER	(OP_LEVEL)0x09

#define TRANSFER_S2C	(uint8_t)0x00
#define TRANSFER_C2S	(uint8_t)0x01	/* currently unused */


typedef struct point POINT;
struct point
{
	uint16_t	x;
	uint16_t	y;
};


typedef struct arena_list ARENA_LIST;
struct arena_list
{
	char name[16];		/* arena name */
	uint16_t count; 	/* number of players here */
	int current_arena;	/* set if this is the arena the bot is in */
};

/*
 * Contains player data.
 */
typedef struct PLAYER_ PLAYER;
struct PLAYER_ {
	PLAYER*		here_next;	/* next player in here list */
	PLAYER*		here_prev;	/* previous player in here list */
	PLAYER*		spec_next;	/* next player in spec list */
	PLAYER*		spec_prev;	/* previous player in spec list */
	PLAYER*		ship_next;	/* next player in ship list */
	PLAYER*		ship_prev;	/* previous player in ship list */

	char		name[20];	/* player's name */
	char		squad[20];	/* squad name */

	PLAYER_ID	pid;		/* pid value retrieved from server */

	ticks_ms_t	enter_tick;	/* tick of when the player entered */
	ticks_ms_t	leave_tick;	/* tick of when the player left */

	uint8_t		ship;		/* the players ship, SHIP_xxx */
	uint16_t	freq;		/* the players freq */

	POINT		pos[1];		/* player position */
	uint16_t	x_vel;		/* x velocity */
	uint16_t	y_vel;		/* y velocity */

	uint8_t		status;		/* player's status (bitfield) see STATUS_xxx */

	uint8_t		in_arena;	/* non-zero if player is in the arena */

	struct {
		int valid;		/* non-zero when the entry is valid */

		uint32_t mid;		/* machine id */
		uint32_t ip;		/* ip address in host format */

		struct {
			ticks_ms_t current;	
			ticks_ms_t low;
			ticks_ms_t high;
			ticks_ms_t average;
		} ping[1];

		struct {
			float s2c;
			float c2s;
			float s2c_wep;
		} ploss[1];

	} info[1];

	struct {
		int	valid;		/* non-zero when the entry is valid */

		uint32_t userid;	/* user id */
		struct {
			uint16_t x;
			uint16_t y;
		} res[1];

		ticks_ms_t	idle_ticks;
		uint32_t	timer_drift;
	} einfo[1];
};

/*
 * Data from the core that is exported to event handlers.
 */
typedef struct core_data CORE_DATA;
struct core_data
{
	int	  event;	/* the event being exported */

	PLAYER   *p1;		/* player 1 */
	PLAYER	 *p2;		/* player 2 */

	char	 *msg;		/* the content of the message */
	char	 *msg_name;	/* the name of the player, may be "" (zone/arena msgs */
	MSG_TYPE  msg_type;	/* the type of msg, see MSG_xxx */
	char	 *msg_chatname;	/* name of the chat, may be NULL */
	int	  msg_chatid;	/* id of the chat, range 1 .. n, 0 if unset */

	char	 *alert_name;	/* the name of the player who sent the ?alert */
	char	 *alert_arena;	/* the arena the ?alert was sent from */
	char	 *alert_msg;	/* the text of the ?alert */
	char	 *alert_type;	/* the type of the alert */

	SHIP	  old_ship;	/* the players old ship */
	FREQ	  old_freq;	/* the players old freq */

	long	  timer_id;	/* unique timer id of the timer expiring */
	void	 *timer_data1;	/* data1 as passed to SetTimer() */
	void	 *timer_data2;	/* data2 as passed to SetTimer() */

	char	 *cmd_name;	/* name of player issuing command */
	CMD_TYPE  cmd_type;	/* type of command */
	int	  cmd_argc;	/* number of arguments, including command name */
	int	  cmd_level;	/* access level of player issuing command */
	char	**cmd_argv;	/* individual arguments on command line */
	char	**cmd_argr;	/* remaining arguments on command line */

	uint8_t	  transfer_success;	/* set if the transfer was successful */
	uint8_t	  transfer_direction;	/* TRANSFER_S2C or TRANSFER_C2S */
	char	 *transfer_filename;	/* file name */

	char	 ac_old_arena[16];	/* arena change old arena */

	ARENA_LIST *arena_list;	/* EVENT_ARENA_LIST */
	int arena_list_count;

	/* The below are always set during events (where they would be valid) */
	PLAYER_ID bot_pid;	/* the bot's pid */
	char	 *bot_name;	/* the bot's name */
	FREQ	  bot_freq;	/* the bot's freq */
	SHIP	  bot_ship;	/* the bot's ship */
	char	 *bot_arena;	/* the bot's arena */

	PLAYER	 *here_first;	/* head of here list */
	PLAYER	 *here_last;	/* tail of here list */
	PLAYER	 *spec_first;	/* head of here list */
	PLAYER	 *spec_last;	/* tail of here list */
	PLAYER	 *ship_first;	/* head of here list */
	PLAYER	 *ship_last;	/* tail of here list */
	int	  here_count;	/* number of players in the arena */
	int	  spec_count;	/* number of players in spec */
	int	  ship_count;	/* number of players in a ship */

	void	 *user_data;	/* The thread-specific memory pointer */

	void	**pinfo_base;	/* The pinfo array base, used for pinfo */
	PLAYER	 *parray;	/* The player array base, used for pinfo */
	int	  phere;	/* number of used player slots */
};

/*
 * The plugin's game event callback.
 */
typedef void (*GameEvent_cb)(CORE_DATA *cd);


/*
 * Contains library data.
 */
typedef struct LIB_DATA_ LIB_DATA;
struct LIB_DATA_ {
	char	*name;
	char	*author;
	char	*version;
	char	*date;
	char	*time;
	char	*description;
	char	*oc_version;
	int	 pinfo_size;

	GameEvent_cb cb;
};

/*
 * This is the callback prototype used for commands. Commands must all be
 *  registered with the RegisterCommand() function.
 *
 * 'name' is always set to the name of the player who issued the command.
 * 'level' is always set to the player's bot access level.
 * 'cmd_type' is one of CMD_xxx, indicating the type of message used to
 * 		issue the command. NOT interchangeable with MSG_xxx.
 * 'argc' is the number of arguments sent to the command, including the
 * 		command name itself. argc-1 is the largest valid index
 *		into the argv and argr arrays.
 * 'argv' is an array of pointers to strings that contain each sequential
 * 		command argument. argv[0] is the command itself.
 * 'argr' is an array of pointers to strings that contain each argument and
 * 		everything that follows it. argr[1] is the first
 *		argument and everything after it.
 */
typedef void (*Command_cb)(CORE_DATA *cd);

/*
 * Registers a command and its callback handler with the core. 
 *
 * 'cmd_text' is the actual command. It must begin with a '!'.
 * 'cmd_class' a one-word class/category that the command shows up in
 *     when a player views !help.
 * 'req_level' the op level that a player has to have to use the command.
 * 'cmd_type' the class of messages that this command can be sent from. One
 *     of CMD_xxx.
 * 'cmd_args' arguments to the command. May be NULL. If a command has
 *		arguments, they should be specified UNIX-style. Angle
 *		brackets enclose required commands, square brackets
 *		enclose optional arguments.
 * 'cmd_desc' a short description of the plugin. May be NULL.
 * 'cmd_ldesc' a long description of the command. May be NULL.
 * 'func' the command handler callback.
 */
void	RegisterCommand(char *cmd_text, char *cmd_class, int req_level,
	    CMD_TYPE cmd_type, char *cmd_args, char *cmd_desc,
	    char *cmd_ldesc, Command_cb func);

/*
 * Set a timer.  It will expire in 'duration' milliseconds, causing
 * EVENT_TIMER to occur. SetTimer() returns a unique timer id
 * that is passed as 'timer_id' in EVENT_TIMER. 'data1' and 'data2'
 * are values that are passed back during EVENT_TIMER in 'timer_data1' 
 * and 'timer_data2'. This can be used to store user-specified data, or
 * can be NULL if they are unused. Timers are only as granular as
 * 100ms.
 *
 * To handle multiple timers, store SetTimer's return value and
 * check it against CORE_DATA.timer_id during EVENT_TIMER.
 */ 
long	SetTimer(ticks_ms_t duration, void *data1, void *data2);

/*
 * Kill a timer with id of 'timer_id'
 */
void	KillTimer(long timer_id);

/*
 * Log a message to the log system. 'lvl' is the required op level to view
 * in !log.
 */
void	Log(OP_LEVEL lvl, char *str); 
void	LogFmt(OP_LEVEL lvl, char *fmt, ...);

/*
 * Functions for starting and stopping bots.
 */ 
char*	StartBot(char *type, char *arena, char *owner);	/* returns error string on error */
void	StopBot(char *reason);
void	StopBotFmt(char *fmt, ...);

/*
 * Change the bot's arena.
 */
void	Go(char *arena);

/*
 * Set the bot's x and y coordinates (in pixels)
 */
void	SetPosition(uint16_t x, uint16_t y, uint16_t xv, uint16_t yv);

/*
 * Queue a file for transfer. The file will be transferred as soon
 * as possible and an EVENT_TRANSFER event will be generated when
 * the transfer completes.
 *
 * Returns 1 on success, 0 on failure. Filenames containing
 * '..', '\', or '/' fail.
 */
int	QueueGetFile(const char *filename, const char *initiator);
int	QueueSendFile(const char *filename, const char *initiator);

/*
 * Get the oplevel for 'name', as defined in 'ops.conf'.
 */
int	GetOpLevel(char *name);

/*
 * Search functions:
 *
 * FindPlayerName:
 * Find a player with the name of 'name'.  The player may
 * or may not be in the arena. PLAYER.in_arena will be set to 1
 * if the player is in the arena.
 *
 * FindPlayerPid:
 * Search for a player by his player id. This only searches
 * players who are currently in the arena.  Players who are not
 * in the arena do not have valid ids since they may have left
 * the zone and the server may have reassigned the pid to someone
 * else.
 */
PLAYER*	FindPlayerName(char *name, MATCH_TYPE match_type);
PLAYER*	FindPlayerPid(PLAYER_ID pid, MATCH_TYPE match_type);

/*
 * Message Functions:
 *
 * Various functions for sending messages. RmtMessage* can be used
 * for generic priv message sending such as in command handlers.
 */
void	ArenaMessage(const char *msg);
void	ArenaMessageFmt(const char *fmt, ...);
void	ChatMessage(const char *chat, const char *msg);
void	ChatMessageFmt(const char *chat, const char *fmt, ...);
char*	ChatName(int number);	/* 1 .. n */
void	PubMessage(const char *msg);
void	PubMessageFmt(const char *fmt, ...);
void	PrivMessage(PLAYER *p, const char *msg);
void	PrivMessageFmt(PLAYER *p, const char *fmt, ...);
void	RmtMessage(const char *name, const char *msg);
void	RmtMessageFmt(const char *name, const char *fmt, ...);
void	TeamMessage(const char *msg);
void	TeamMessageFmt(const char *fmt, ...);
void	FreqMessage(PLAYER *p, const char *msg);
void	FreqMessageFmt(PLAYER *p, const char *fmt, ...);
/*
 * These are only valid inside EVENT_COMMAND.
 */
void	Reply(const char *msg);
void	ReplyFmt(const char *fmt, ...);

/*
 * Copy a field from a string into a buffer.
 * 
 * 'dst' is the destination
 * 'len' is the size of the destination buffer
 * 'src' is the source string
 * 'arg' is the 0-based column to extract
 * 'delim' is the delimiter between columns
 * 'getremain' specifies whether or not to retrieve everything
 * after the specified column, including delimiters
 */
void	DelimArgs(char *dst, int len, char *src, int arg, char delim, bool getremain);

/*
 * Count the number of arguments in a 'delim'-delimted string.
 *
 * For example, assuming a ':'-delimited string:
 * "" -> 0
 * "asdf" -> 1
 * "asdf:asdf" -> 2
 * ...
 */
int	ArgCount(char *str, char delim);

/*
 * Like DelimArgs, except the argument is converted to an integer.
 */
int	AtoiArg(char *src, int arg, char delim);

/*
 * Count the number of delimiters in a string.
 */
int	DelimCount(char *str, char delim);

/*
 * Convert ticks to a readable string.
 */
void	TicksToText(char *dst, int dstl, ticks_ms_t ticks);

/*
 * Determines the type of an arena based on name.
 */
int	IsPub(char *arena);		/* 0-99 */
int	IsSubarena(char *arena);	/* non-main pub, ex: duel, baseduel, ladder */
int	IsPriv(char *arena);		/* priv */

/*
 * Get the tick count.
 */
ticks_ms_t	GetTicksMs();

/*
 * Retrieve a player-specific pinfo struct.  This is only valid if
 * LIB_PINFO_SIZE is set.
 */
inline
void*
GetPlayerInfo(CORE_DATA *cd, PLAYER *p)
{
	return cd->pinfo_base[p - cd->parray];
}

/*
 * Wrappers for common functions.
 */
void	*xcalloc(size_t nmemb, size_t sz);
void	*xmalloc(size_t sz);
void	*xzmalloc(size_t sz);

void	strlwr(char *str);

/*
 * Each of these must be defined in the library.
 */
extern LIB_DATA REGISTRATION_INFO;	/* single word library name */

#ifdef __cplusplus
}
#endif

#endif /* _LIBOPENCORE_HPP */

