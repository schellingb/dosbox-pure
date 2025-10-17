/*
Copyright (C) 2020-2025 Bernhard Schelling

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
*/

const path_yml = "keyb2joypad.yml";
const path_output = "keyb2joypad";

// Constants and lookup tables
const YMLKEYBOARD = ['none', '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', 'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', 'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', 'z', 'x', 'c', 'v', 'b', 'n', 'm', 'F1', 'F2', 'F3', 'F4', 'F5', 'F6', 'F7', 'F8', 'F9', 'F10', 'F11', 'F12', 'ESC', 'TAB', 'BACKSPACE', 'ENTER', 'SPACE', 'LEFTALT', 'RIGHTALT', 'LEFTCTRL', 'RIGHTCTRL', 'LEFTSHIFT', 'RIGHTSHIFT', 'CAPSLOCK', 'SCROLLLOCK', 'NUMLOCK', 'GRAVE', 'MINUS', 'EQUALS', 'BACKSLASH', 'LEFTBRACKET', 'RIGHTBRACKET', 'SEMICOLON', 'QUOTE', 'PERIOD', 'COMMA', 'SLASH', 'EXTRA_LT_GT', 'PRINTSCREEN', 'PAUSE', 'INSERT', 'HOME', 'PAGEUP', 'DELETE', 'END', 'PAGEDOWN', 'LEFT', 'UP', 'DOWN', 'RIGHT', 'KP1', 'KP2', 'KP3', 'KP4', 'KP5', 'KP6', 'KP7', 'KP8', 'KP9', 'KP0', 'KPDIVIDE', 'KPMULTIPLY', 'KPMINUS', 'KPPLUS', 'KPENTER', 'KPPERIOD'];
const YMLACTIONS200UP = ['mouse_move_up', 'mouse_move_down', 'mouse_move_left', 'mouse_move_right', 'mouse_left_click', 'mouse_right_click', 'mouse_middle_click', 'mouse_speed_up', 'mouse_speed_down', 'joy_up', 'joy_down', 'joy_left', 'joy_right', 'joy_button1', 'joy_button2', 'joy_button3', 'joy_button4', 'joy_hat_up', 'joy_hat_down', 'joy_hat_left', 'joy_hat_right', 'joy_2_up', 'joy_2_down', 'joy_2_left', 'joy_2_right', 'on_screen_keyboard', 'wheel'];

const YMLKeys = new Map();
YMLKEYBOARD.slice(1).forEach((key, i) => YMLKeys.set(key.toLowerCase(), i + 1));
YMLACTIONS200UP.forEach((key, i) => YMLKeys.set(key.toLowerCase(), 200 + i));

const WHEEL_BTN_ID = 20;
const YMLBTNNAMES = ["b", "y", "select", "start", "up", "down", "left", "right", "a", "x", "l", "r", "l2", "r2", "l3", "r3"];

const YMLBtnIds = new Map();
YMLBTNNAMES.forEach((name, i) => YMLBtnIds.set(name, i));
YMLBtnIds.set('lstick_left', 16);
YMLBtnIds.set('lstick_right', 16);
YMLBtnIds.set('lstick_up', 17);
YMLBtnIds.set('lstick_down', 17);
YMLBtnIds.set('rstick_left', 18);
YMLBtnIds.set('rstick_right', 18);
YMLBtnIds.set('rstick_up', 19);
YMLBtnIds.set('rstick_down', 19);

// Prepare buckets
const BUCKETS = 4, TBLSZ = 1060 * BUCKETS, buckets = [];
const COMMON_ACTIONS = [ "Start" /* 3322 */, "Move Left/Right" /* 3279 */, "Move Up/Down" /* 3019 */, "Pause" /* 2975 */, "Move Left" /* 2582 */, "Move Right" /* 2580 */, "Move Up" /* 2507 */, "Move Down" /* 2500 */, "Quit to Title" /* 2209 */, "Fire" /* 2000 */, "Jump" /* 748 */, "Enter Key" /* 540 */, "Accelerate" /* 466 */, "Help" /* 461 */ ];
for (let i = 0; i < BUCKETS; i++)
{
	const BK = { map_data: [], map_length: 0, action_idx: new Map(), action_data: [], action_length: 0, title_idx: new Map(), title_data: [], title_length: 0, idents: new Array(TBLSZ / BUCKETS).fill(Buffer.alloc(5)) };
	for (const action of COMMON_ACTIONS)
	{
		BK.action_idx.set(action, BK.action_length);
		BK.action_data.push(Buffer.from(action + '\0', 'utf8'));
		BK.action_length += BK.action_data[BK.action_data.length - 1].length;
	}
	buckets.push(BK);
}

var done_games = 0;
const map_ids = new Map(), done_hashes = new Map(), action_count = new Map(), map_keys = new Array(TBLSZ).fill(0);

// Parse YML
console.log(`Loading ${path_yml} ...`);
const fs = require('fs');
for (let ymlLines = fs.readFileSync(path_yml, 'utf8').replace(/\r/g, '').split('\n'), lineidx = 0; lineidx < ymlLines.length;)
{
	// Read next game data
	let gamename, gameyear, maps = [], id_files = [], id_sizes = [];
	for (let wheelnum = 0, parts; lineidx < ymlLines.length; )
	{
		const line = ymlLines[lineidx++];
		if      (line.startsWith('-')) { if (gamename) break; }
		else if (parts = line.match(/^\s*name:\s*"(.*)"/)) { if (gamename) throw new Error(`Multiple gamename keys for ${gamename}`); gamename = parts[1]; }
		else if (parts = line.match(/^\s*year:\s*(\d+)/)) { if (gameyear) throw new Error(`Multiple gameyear keys for ${gamename}`); gameyear = parseInt(parts[1]); }
		else if (parts = line.match(/^\s*identifier_\d+:\s*(\S+?)\s*,\s*(\d+)/)) { id_files.push(parts[1]); id_sizes.push(parseInt(parts[2])); }
		else if (parts = line.match(/^\s*input_(pad|wheel)_(\w+?)(?:_(\w+)|):\s*(\S+)(?:\s+(.*?)|)\s*(#|$)/))
		{
			// Split input mapping details
			const is_wheel = parts[1] === "wheel";
			const btn_id = is_wheel ? WHEEL_BTN_ID : YMLBtnIds.get(parts[2] + (parts[3] ? `_${parts[3]}` : ''));
			const is_analog = (btn_id >> 2) === 4;
			const analogpart = (parts[3] === "down" || parts[3] === "right") ? 1 : 0;
			let keys = parts[4].toLowerCase().split('+').map(key => YMLKeys.get(key));
			let action = (parts[5] || '');

			// Validate input mapping
			if (btn_id === undefined) throw new Error(`Unknown button mapping in YML '${line}' of game '${gamename}'\n`);
			if (is_wheel && (parseInt(parts[2]) !== ++wheelnum)) throw new Error(`Wheel number out of order in YML '${line}' of game '${gamename}'\n`);
			if (keys.length === 0) throw new Error(`No keys in button mapping '${line}' of game '${gamename}'\n`);
			if (keys.length > 3) throw new Error(`More than 3 keys in button mapping '${line}' of game '${gamename}'\n`);
			for (const key of keys) { if (key === undefined) throw new Error(`Unknown key listed in button mapping '${line}' of game '${gamename}'\n`); }

			let old_idx = (is_wheel ? maps.length : maps.findIndex(map => (map.readUInt8(0) & 31) === btn_id));
			if (!is_wheel && !is_analog && old_idx !== -1) throw new Error(`Duplicated input mapping '${line}' of game '${gamename}'\n`);

			// For analog axis mappings, two directional parts are combined into one mapping
			if (is_analog && old_idx !== -1) // second part
			{
				const old_map = maps[old_idx], old_btn_id = old_map.readUInt8(0), action_end = ((old_btn_id & 32) ? old_map.indexOf(0, 1) : 0);
				let old_action = ((old_btn_id & 32) ? old_map.toString('utf8', 1, action_end) : ''), old_data = old_map.subarray(action_end + 1);
				if (old_data.readUInt8(analogpart) !== 0) throw new Error(`Duplicated input mapping '${line}' of game '${gamename}'\n`);
				const aparts = old_action.split('\x01'), action0 = (analogpart ? aparts[0] : action), action1 = (analogpart ? action : aparts[1]);
				const m0 = action0.match(/^(\S+) (.+)$/), verb0 = (m0 ? m0[1] : null), m1 = action1.match(/^(\S+) (.+)$/), verb1 = (m1 ? m1[1] : null);
				action = ((verb0 && verb0 === verb1) ? `${verb0} ${m0[2]}\x01${m1[2]}` : ((action0 || action1) ? `${action0 || 'Nothing'}\x01${action1 || 'Nothing'}` : ''));
				while (old_data.length / 2 > keys.length) keys.push(0); // fill up with none entries if there are less keys on this part
				for (let i = keys.length; i; i--) { let o = i * 2 - 1 - analogpart; keys.splice(i - analogpart, 0, (o < old_data.length ? old_data.readUInt8(o) : 0)); } // merge
			}
			else if (is_analog) // first part
			{
				if (action) action = (analogpart ? `Nothing\x01${action}` : `${action}\x01Nothing`);
				for (let i = keys.length; i; i--) keys.splice(i - analogpart, 0, 0); // insert none entries for the other analogpart
			}

			// Store encoded mapping
			const enc_btn_id = (((keys.length / (is_analog ? 2 : 1)) - 1) << 6) | (action.length > 0 ? 32 : 0) | btn_id;
			const action_buffer = Buffer.from((action.length > 0 ? (action + '\0') : ''), 'utf8');
			const mapBuffer = Buffer.concat([Buffer.from([enc_btn_id]), action_buffer, Buffer.from(keys)]);
			if (old_idx === -1) maps.push(mapBuffer); else maps[old_idx] = mapBuffer;
		}
		else if (line && !line.startsWith('#')) throw new Error(`Unknown YML entry '${line}' on line ${lineidx-1}\n`);
	}

	if (!gamename && (gameyear || maps || id_files || id_sizes)) { throw new Error(`Had data but not a game name at the end of the YML file`); }
	if (!gamename) break;
	if (!gameyear) { throw new Error(`Game ${gamename} with no year info`); }
	if (!maps)     { throw new Error(`Game ${gamename} (${gameyear}) with no input mappings`); }
	if (!id_files) { throw new Error(`Game ${gamename} (${gameyear}) with no identifiers`); }

	// If multiple games have identical mappings, store each set only once
	const mapkey = maps.map(m => m.toString('binary')).join('');
	let mapid = map_ids.get(mapkey);
	if (!mapid)
	{
		const bknum = (map_ids.size % BUCKETS), BK = buckets[bknum];
		if (BK.map_length > 0xFFFF) throw new Error("Map data too large, need more buckets\n");
		if (maps.length > 0xFF) throw new Error(`Game ${gamename} has more than 255 mappings\n`);

		// MapID is the bucket number and data offset to the mapping data
		const mapIdBuffer = Buffer.alloc(3);
		mapIdBuffer.writeUInt8(bknum, 0);
		mapIdBuffer.writeUInt16BE(BK.map_length, 1);
		map_ids.set(mapkey, mapIdBuffer);
		mapid = mapIdBuffer;

		// Start the mapping data block with the number of elements
		const map_count_buf = Buffer.alloc(1);
		map_count_buf.writeUInt8(maps.length, 0);
		BK.map_data.push(map_count_buf);
		BK.map_length += map_count_buf.length;

		for (map of maps)
		{
			const btn_id = map.readUInt8(0);
			if (btn_id & 32) // Has an action name string
			{
				const action_end = map.indexOf(0, 1), action = map.toString('utf8', 1, action_end).replace('\x01', '/'), data = map.subarray(action_end + 1);

				// Find the action name string in the pool, or add a new entry
				let action_idx = BK.action_idx.get(action);
				if (action_idx === undefined)
				{
					BK.action_idx.set(action, (action_idx = BK.action_length));
					BK.action_data.push(Buffer.from(action + '\0', 'utf8'));
					BK.action_length += BK.action_data[BK.action_data.length - 1].length;
				}

				// Now generate the actual mapping data which has the name string replaced with a BER compressed index to the action name
				const action_idx_len = (action_idx < 0x80 ? 1 : (action_idx < 0x4000 ? 2 : (action_idx < 0x200000 ? 3 : 4)));
				map = Buffer.alloc(1 + action_idx_len + data.length);
				map.writeUInt8(btn_id, 0);
				map.writeUInt8(((action_idx >> ((action_idx_len-1)*7)) & 0x7F) | (action_idx_len == 1 ? 0 : 0x80), 1);
				if (action_idx_len > 1) map.writeUInt8(((action_idx >> ((action_idx_len-2)*7)) & 0x7F) | (action_idx_len == 2 ? 0 : 0x80), 2);
				if (action_idx_len > 2) map.writeUInt8(((action_idx >> ((action_idx_len-3)*7)) & 0x7F) | (action_idx_len == 3 ? 0 : 0x80), 3);
				if (action_idx_len > 3) map.writeUInt8(((action_idx >> ((action_idx_len-4)*7)) & 0x7F), 4);
				data.copy(map, 1 + action_idx_len);

				action_count.set(action, (action_count.get(action) || 0) + 1);
			}
			BK.map_data.push(map);
			BK.map_length += map.length;
		}
	}

	// Store the list of identifiers for this game
	for (let i = 0; i < id_files.length; i++)
	{
		const id_file = id_files[i], id_size = id_sizes[i], hash_value = DBP_HashSize(DBP_Hash(id_file), id_size);
		const desc = `Identification file ${id_file} (size: ${id_size}) for game ${gamename}`;
		if (done_hashes.has(hash_value) && done_hashes.get(hash_value) !== desc) throw new Error(`DUPLICATE ${desc} [${done_hashes.get(hash_value)}]\n`);
		done_hashes.set(hash_value, desc);

		let hash_idx = (hash_value % TBLSZ);
		for (let x = 0; map_keys[hash_idx] !== 0; x++) { hash_idx = (hash_idx + 1) % TBLSZ; if (x === TBLSZ) throw new Error(`HASH TABLE FULL @ ${gamename}`); }
		map_keys[hash_idx] = hash_value;
		const BK = buckets[hash_idx % BUCKETS];

		let title_key = `${gamename} (#${gameyear})`, title_idx = BK.title_idx.get(title_key);
		if (title_idx === undefined)
		{
			if (!gameyear || gameyear < 1970 || gameyear > 1970 + 255) throw new Error(`Invalid year ${gameyear} for game ${gamename}`);
			BK.title_idx.set(title_key, BK.title_length);
			title_idx = BK.title_length;
			if (title_idx > 0xFFFF) throw new Error("Title data too large, need more buckets\n");

			const titleBuf = Buffer.alloc(1 + gamename.length + 1);
			titleBuf.writeUInt8(gameyear - 1970, 0);
			titleBuf.write(gamename, 1, 'utf8');
			titleBuf.writeUInt8(0, gamename.length + 1);
			BK.title_data.push(titleBuf);
			BK.title_length += titleBuf.length;
		}

		const identBuf = Buffer.alloc(mapid.length + 2);
		mapid.copy(identBuf, 0);
		identBuf.writeUInt16BE(title_idx, mapid.length);
		BK.idents[Math.floor(hash_idx / BUCKETS)] = identBuf;
	}

	done_games++;
}
console.log('Done!\n');

console.log(`Games: ${done_games}`);
console.log(`Hash Table Entries: ${done_hashes.size}`);
console.log(`Hash Table Fill Rate: ${((done_hashes.size / TBLSZ) * 100).toFixed(2)}%`);

let common_actions = '', sorted = [...action_count.entries()].sort(([, a], [, b]) => a - b).map(([key]) => key);
for (let i = sorted.length, l = 0, a; l < 127 && i--;) { a = sorted[i]; l += a.length + 1; common_actions += `"${a}" /* ${action_count.get(a)} */, `; }
console.log(`Common Actions: ${common_actions}`);

const hContent = 
	'#include "include/config.h"' + "\n" +
	'' + "\n" +
	'struct MAPBucket' + "\n" +
	'{' + "\n" +
	'	const Bit8u* idents_compressed;' + "\n" +
	'	Bit32u idents_size_compressed;' + "\n" +
	'	Bit32u idents_size_uncompressed;' + "\n" +
	'	const Bit8u* mappings_compressed;' + "\n" +
	'	Bit32u mappings_size_compressed;' + "\n" +
	'	Bit32u mappings_size_uncompressed;' + "\n" +
	'	Bit32u mappings_action_offset;' + "\n" +
	'};' + "\n" +
	'' + "\n" +
	'enum { MAP_TABLE_SIZE = '+TBLSZ+', MAP_BUCKETS = '+BUCKETS+' };' + "\n" +
	'extern const Bit32u map_keys[MAP_TABLE_SIZE];' + "\n" +
	'extern const MAPBucket map_buckets[MAP_BUCKETS];' + "\n";

let cppContent =
	'// Original data from the Keyb2Joypad Project' + "\n" +
	'// Copyright Jemy Murphy and bigjim - Used with permission' + "\n" +
	'// Amendments and fixes done by the DOSBox Pure project' + "\n" +
	'' + "\n" +
	'#include "keyb2joypad.h"' + "\n" +
	'' + "\n" +
	'const Bit32u map_keys[MAP_TABLE_SIZE] = {';

for (let i = 0; i < TBLSZ; i++)
{
	if (i) cppContent += ",";
	if ((i % 128) === 0) cppContent += "\n\t";
	cppContent += map_keys[i];
}

cppContent += "\n};\n\n" +
	'const MAPBucket map_buckets[MAP_BUCKETS] =' + "\n" +
	'{' + "\n";

let compressedtotal = 0;
for (let i = 0; i < BUCKETS; i++)
{
	const BK = buckets[i];
	const idents_buf = Buffer.concat(BK.idents), title_buf = Buffer.concat(BK.title_data), map_buf = Buffer.concat(BK.map_data), action_buf = Buffer.concat(BK.action_data);
	console.log(`\nBucket [${i}]:`);
	console.log(`	Idents: Count = ${BK.idents.length} - Bytes = ${idents_buf.length} - Hash = ${DBP_Hash(idents_buf.toString('binary'))}`);
	console.log(`	Title:  Count = ${BK.title_idx.size} - Bytes = ${BK.title_length} - Hash = ${DBP_Hash(title_buf.toString('binary'))}`);
	console.log(`	Map:    Count = ${BK.map_data.length} - Bytes = ${BK.map_length} - Hash = ${DBP_Hash(map_buf.toString('binary'))}`);
	console.log(`	Action: Count = ${BK.action_idx.size} - Bytes = ${BK.action_length} - Hash = ${DBP_Hash(action_buf.toString('binary'))}`);

	const idents_uncompressed_buf = Buffer.concat([ idents_buf, title_buf ]), idents_compressed_buf = ZopfliDeflate(idents_uncompressed_buf), idents_c = BufferToBinaryC(idents_compressed_buf);
	console.log(`	Idents + Title: Compressed ${idents_uncompressed_buf.length} to ${idents_compressed_buf.length} bytes, encode as ${idents_c.length} character C-string`);
	compressedtotal += idents_compressed_buf.length;

	const mappings_uncompressed_buf = Buffer.concat([ map_buf, action_buf ]), mappings_compressed_buf = ZopfliDeflate(mappings_uncompressed_buf), mappings_c = BufferToBinaryC(mappings_compressed_buf);
	console.log(`	Map + Action: Compressed ${mappings_uncompressed_buf.length} to ${mappings_compressed_buf.length}, encode as ${mappings_c.length} character C-string`);
	compressedtotal += mappings_compressed_buf.length;

	cppContent += `\t{\n`;
	cppContent += `\t\t(const Bit8u*)${idents_c},\n`;
	cppContent += `\t\t${idents_compressed_buf.length},\n`;
	cppContent += `\t\t${idents_uncompressed_buf.length},\n`;
	cppContent += `\t\t(const Bit8u*)${mappings_c},\n`;
	cppContent += `\t\t${mappings_compressed_buf.length},\n`;
	cppContent += `\t\t${mappings_uncompressed_buf.length},\n`;
	cppContent += `\t\t${BK.map_length},\n`;
	cppContent += `\t},\n`;
}

cppContent += `};\n`;

console.log(`\nCompressed Total: ${compressedtotal}`);
console.log(`Map Table Size: ${TBLSZ * 4}`);
console.log(`Binary Blob Total: ${compressedtotal + TBLSZ * 4}`);

console.log(`\nWriting ${path_output}.h ...`);
fs.writeFileSync(path_output+'.h', hContent);
console.log(`\nWriting ${path_output}.cpp ...`);
fs.writeFileSync(path_output+'.cpp', cppContent);

console.log('\nDone!\n');

function DBP_HashSize(hash, size)
{
	return (hash ^ (size << 3)) >>> 0;
}

function DBP_Hash(s)
{
	let hash = 0x811c9dc5;
	for (let i = 0; i < s.length; i++) hash = ((((0x01000000 * hash) | 0) + (0x00000193 * hash)) | 0) ^ s.charCodeAt(i);
	return hash >>> 0;
}

function BufferToBinaryC(buf)
{
	if (buf.length > 65510) throw new Error(`Too long to string encode (${buf.length}), use byte array notation...\n`);
	let res = '"';
	for (let lineBreakAt = 950, codeLineLen = 0, mode = 0, i = 0, p, a; i < buf.length; i++)
	{
		switch (p = buf[i])
		{
			case 8: a = '\\b'; mode = 0; break;
			case 12: a = '\\f'; mode = 0; break;
			case 10: a = '\\n'; mode = 0; break;
			case 13: a = '\\r'; mode = 0; break;
			case 9: a = '\\t'; mode = 0; break;
			case 11: a = '\\v'; mode = 0; break;
			case 34: a = '\\"'; mode = 0; break;
			case 92: a = '\\\\'; mode = 0; break;
			case 63: a = (mode === 2 ? '\\' : '') + '?'; mode = 2; break; // mode = in trigraph
			default:
				if (p >= 48 && p <= 57) a = (mode === 1 ? '\\' + p.toString(8) : String.fromCharCode(p));
				else if (p >= 32 && p <= 126) { a = String.fromCharCode(p); mode = 0; }
				else if (p === 0 && i === buf.length - 1) a = ''; // use null terminator
				else { a = '\\' + p.toString(8); mode = 1; } // #mode = in octal
				break;
		}
		if ((codeLineLen += a.length) > lineBreakAt) { res += '"\n\t\t"'; codeLineLen = 0; }
		res += a;
	}
	res += '"';
	return res;
}

function ZopfliDeflate(buf)
{
	// Zopfli - Copyright 2011 Google Inc. All Rights Reserved. Licensed under the Apache License, Version 2.0
	var WA=WA||{};(()=>{var B,J,Z,k,c=WA.print||(WA.print=B=>console.log(B.replace(/\n$/,""))),L=WA.error||(WA.error=(B,J)=>c("[ERROR] "+B+": "+J+"\n")),N=71408,F=WA.maxmem||268435456,g=WA.abort=(B,J)=>{throw L(B,J),"abort"},T=()=>{var B=J.buffer;Z=new Uint8Array(B)},K=(B=>{for(var J,Z=0,k=0,c=Uint8Array,L=new c(128).map((B,J)=>J<92?J-58:J-59),N=new c(27581),F=B=>L[":>qVh?::::J:_@J?Z?kY::q:zxrY;kAR<wrY::@;zxrYz;:R;wQ:z;P;zxrYzxA:ZGkYzxQ:z;a:zxQ:z;q:zxrY::@:;wAR;kQ:w;@:::a;zxrYzxrY::P<zxrYzxrYzxrY::@;zxrYz?:YZOkYzxrYz?kYZWkYzxrYzxrY;wAR?wrYzxrY;kARBwrYzxrYzxrY::@=zxrYzxrYzxrYzxrY::@<zxrYzxrYzxQ:zCkF?FJSiSQ;YxOTnC@:CFJSiSA;nCaVf;:;=NaUqG:UjW@:DFJSiSA;`[QTo;::;cN:[;Z:=V^GEJ::>JZ;>:k;BNZ:<RZ;@::::Fk:;k::<ok::BZ=<wJ;J>l::VZ>?FJ:MJl:?BJ:BZk:AN:<>NJ:k?J;?Nk:;:Z:@sZ:z?JJkrD;EwA:;[BCEVZJ?RJU`pqUm`b::>lQYWQRnpqQ^?@UgxpRoxaVn;J;@oPRglqU^;k<LwpQ`pqQg`aRYKPSkGrQ_O@VnGJ:=BOOH;ZHCcJ::>N:EJZ>ss<GDNVi;R^I;BkY=:>B;>>c_caB::<:qB:;Z::B:R^:B:L:;;[T]>JJ:]kKG:k<;;=>@cKJBrLJJn]G<:JJ:qLJeR^::gJs<>:;z;<:;WZT;[QV]>:>;B<:;xrK>:>>:c;>:c;>:c;>:c;>:c;B::L:eCL:;KkT;;[G<::B;>>>fCm:;;[G<:Z:zCkY;;DEcB::]B:;z;]:cB:<?>>:E:<:@K:JZ:JJ>g@D<:JB<:;:T:;:T:<:ZBJJy?rT]BJJ>g@D<:JB>:;:T>>cgZ]:::L:qB:<ZB:B>>_YlhaB;:L:cB::e?>;fp<::>N:lK:JJ:Z@J:Z@J:Z@J:Z@Z>:D<JZB<:L:cB:<]JZG<Z:B>:]:qB:;J:Z@J:Z@J:Z@J:Z@ZF:B;g@==g:B:>>>flJ:E:;:T:;:T:;:T:;:T:<:;;[G<k:B:>>>qB::Z::B;R^:B>>cgBL:cB::[B:B;:<:qB::Z::B<R^:>:<:;;[TE>L:J:Z@J:Z@J:Z@J:Z@ZF:B;gq<[F:B;:m:qB::Z>:B=>>YldPJ>g@B=>N:mS^:::L:JVk<Z:JJzWo<P?Z:z;;:T:;:T:<:Z::D<:JJBg@>CB]:;K:W;;DCeCL:qB:;Z::B;Z]:BR^:B:;:T:L:Z:ZG<Z:B:Z]:B:<:qB:;;[DE]::B:_m::BN:ZBJf@LsG=:k<qIZ:Cwb:y?><[FZ::G:J;;JB@:m:ZFJJ;gPVZ:JJAh_VG>:>:c;>:cKJB:<:;GZT;lQVZ:JJBoo@]::>C><;ZFJJB:m:;[kL]_k@[FJJcmLD=:ZBD:<;h]cBE:OK>:o::;;:T:;:T:;:T:<;ZgZX]ckdeC<;;K:W]VJJZL]T]Z:D<ZJB?:m<Zc:][c:>:c[:y;L;ZZkK>:>B?:m:Z::>DB];GN:>:c;>:c;>:c;>:c;B?Z]:>BL:ZN:D<ZZB<R^:B:]:Z>ZG<J:>:c;>:c;>:c;B?:<<qB:<ZN:BA>>c_caB<Z]:>R^:>:;:T:]:ZNZG<J:B?Z]:>:L;qB:<ZJJJ;cPB>:]<<?:]F>k<;[DE;[DEdF::<tAB>oT]=Xn:::]<<?J[E>m<J:Z@ZgZJ:BO=:gJJcmLD=:JBDgJJz:]<tWuT[VZ::;]<JK:J;;JB;kJ:E:m;;K:W]BJJZL]TcB:<[>:BDB>Z:<BZ>JO=::L:ZBJJZL]T]BZKG:JJVyQB>F:JZJJJ;caB>NN=;:L:ZF:B::[<]RJ==:L:cB:<]>:B<VN=:g:B<>L:E:;:T:<:ZFJJkcPJk:L<Uc@>@o::EB:JZ>JKG::B;:m;;K:W;;DCeC]:@p::=:>B;:m:Z::>DB];GB:B;Z]:BBL:ZBkKG:k<E>>:[Rk<ZRk<Y?J:z;<:;xI:GK:JZ:JJ=RQJ;gq=E:<:b?L:J:Z@J:Z@Z:JJzzKM>wAB:>NAZ>kTq?>;n;L:;C:Wf?_v:cP;;x=B:>_AZ>kTq?_:n;L:;?:Wf?ol:caB::<:;xnMUgk<AEJ:>wA>:c;B;:<:;KZT]FZT;?kT;;:B;gPV]>:B<c@B::<:cB::e?>;fp>;z;;:T:;:T:;:T:;:T:<:cB:;]J:B:Z]:BBL;qB:<ZN:B>R^:>B:JZ>:B=R>;:;<:cB::[>:=;g:B:>>;f[]::><;J:Z@Z::B>>_YlhaB>Z]::>L;Z::D<:JB@:;:T:;:T:;:T:;:T:<;ZN:B;:m:fCL:eCm:qB::ZJ:B=>>YldPJ>g@B=R^:::<:Z>ZT]::B@:L:fCL:qB::EBkYZ>:B<>>@ex>;:;L:ZBkT[F:>:c;>:c;>:c;>:c;B<>><eCL:Z:ZT]B:B=>><fCm:qB::ZB:B=>>YldPJ>g@B=>N:mS^:::]:JV:>:c;>:c;>:c;>:c;B::L:qB::Z::B;>>Yld@=;g:B::L:ehJJ>g@B;R^:::<:;KZT?>>:EgZ;::<:JZk<gAJ:?wAB:J:JJ:Z@Z:JJ>gaB;Z]::>]:J:Z@J:Z@<:>B:>><f[]::B<:Z:JJy?bB=R>;:;L:[::=;g:>:c;>:c;>:c;>:c;>:c;B;:m:fC<:cB:;]J:B:Z]:BBL;qB:<ZN:B>R^:>:]:ZFZT[Bk<Z>:D<:JB=:;:T:L:ZFZT]>:D<:ZB=:L:ZFZT;KkTcB::AK:JJ:Z@J:Z@J:Z@J:Z@Z>:D<JZB=:L:cB:<]JZG<Z:B>:m:qB:;Z>:D<::B<cPB<g:>:c;>:c;>:c;>:c;B::]:qB::Z::B<>>YldPJ>g@B<>N:mS^:::<:JVk<E>C;;ZkY<wAB:N>;:;L:JZ:=;g:B;N>;:;<:JkJJ:kJ:EB:JZ>JJAhN=::;:T:;:T:;:T:;:T:<:<wQJB:L:;GZT;lQVZ>JJBoo@]RJJBcPB;:;:T:;:TB:J<wb::;<:;KkT]ZZB=Z]::B<;ZFZT]B:D<:ZB?:]:ZNZT]NJJ>g@D<:kK>:>>:c;>:c;B?:L:ZFZT]JJJJcqM>:>>:c;>:c;>:c;>:c;B<Z]:>BL<ZB:D<ZZB<R^:B:]:Z_ZG<J:>:c;>:c;>:c;>:c;B>:L;ZJkT]BZG<::B>:]:;lQVe?>;f;]:;?ZVqB::ZJ:>A:;:T:;:T:;:T:;:T:m:Z>ZG<::=>g:B>:L;EpJ:J:Z@J:Z@J:Z@J:Z@ZB:D<JZB;:]:cB:<]JZG<Z:B>:L:qB:;ZF:D<:JB;:]:cB::[B:>:c;>:c;>:c;>:c;B=:L:ZBZT]>ZG<::==g:B>:L:;;[TIK:JJ:Z@J:Z@J:Z@J:Z@ZF:B;R^:::m:Z>JJw?bT;KkTZ>ZG<::>:c;>:c;>:c;>:c;B;:m:eCm:ZJ:B;gaB;R^:::m:Z>JJw?bT;KkTZ>JJ;BbG<::B=:l;;?:=>gJJ;:L:ZJJMG>Z@E>>:EkJ:E:m:Z>JJw?bT;KkTZ>ZG<:JJ;gJ=;c;B@:;<]>JKG::B;:<:ZR:BBZ]::>><fCm;ZR:BA_o@JF[@Z::>F:L:[Vk<ZVk<EcT:;RkY^:JJkAkT]J:C::<;Z:ZG<:JJ;>];<:>B;>_:Bp::ZJJJ>BQBA:<:[Fk::;<:ZFJJFgaB=:]:Z>JJ<gaBB>_:od@D<:kT]NJJ<:[>;;ZM>:>B::m:;C:>L>>:HpZ:E:m;ZN:B=:L;ZFJJ<:[>;;ZM]Nk@]FZG<::BA>>;e?m;ZRJJ;cPB@:L:;?kTZZ:B?g[B;>N:Dp::Eg:B>:];J:;B>>>w;c@C:gZw<>:<zG<:;;Y:fCL<_::BC:<:qB::;?JBD:m:??m<<:_::C:JZ>:D<JZB=:L:cB::]ZJJ;V_V?K:JZ:JB;kJ:E:L<;KZV[k:B:>L:=:>B;BL;ZJ:B<>_:odaB@Z]::gaB;:<:;C:>L>>:FK:JZNJB;kZ:EB:JZgJJzGAB<>_:BCQJ;>QK>:>B@>><f[]::>m;ZNJJFgaB@:L:;C:>L>>:HpJ:ZR:BAg@B;>_:JBKJ:sN=;g:BF:L:qB::<:_::;<<;?kT]NJKG::B?ZaB@NN=::m:[N:BB>m:ZRJBAkJ:E>>:[RZ::;m:?p::ZF:T]VJKG::BA>>Bm?];;;JB?kJ:E:m:[N:BB>m:;;JBAgJJ;>m<ZF:BARQBB:]<;?ZT[c:BF>>;e?<=ZB:B@cPB<:L;ZVZW]F:BB:L;;?:WZVJJzGAWmC<<;?kKmpJ:FFk<E:L;[>:=;g:BENN=;g:BC:]<J:;B;:]:ZJ:>Hg:BC>>w;c@C:gJ_;>k;z?>=[FkB:>>Z<gaB?J<:Z>JJ<s>;:;<:Z>JJ<JbT]V:B?R^::F:JZV:D<::B:Z]::>>Z<:m:ZFJJ:DkMUB<;JF[@Z:JB<:L:[Rk::;]:cB::ZBJJ>caBBZ]:::<;JF[@ZB:B<Z]:::<;eS^:::<<[B:B@>N:fC];G:k<ZF:B>gaB=o::Eg:B?>>Z<c@C:gZ^@>k;z;<:[NkB:>>o;gaB<J<:ZBZJ;Vn:BB:JZ>ZB@>>=gCm;?p::ZBJJFR^:J:]:;lZG<J;B<>>>e?><m?<;;lZB:>L:=:>B>:<:]F:B;>>=edaB:R^:::<;;KZT[J:B=>L:Z::BA_N=:gZ::;L;ZN:BAcPJFgaBAw>;:?N:[J:=;g:B@>N:f?>=g?L:;;JB=>N:[JJJ;><:=:_:z;<:;GJV;GZK>:>B?:<;ZBJJJc@>H:]:ZFJJ<RbB@R^:F:m:;t;WZ:JJ<RbV[::B@>m:ZJJJ<c@=;gZ::;L:ZBJJJcaBB:<;;?kT]RJJ<JbTcB::GK:JZN:B<>><e;<;;;:BB:l=F>k<ZN:B>:]:;;[TJsk<ZJJJ;R>;:;]:ZFJJ;JAB:>oAqCbB=R^:F:<:;?:W[:JJ:kJ:E:]:ZBJJBc@B@>>BCCm:;C:Wm[]:::];ZJJJ[g@B=g[B@JAB:>>:ZFk@]:JJZ:];fSbV]FZG<k:B::];o?<:;?k<[J:B<:<:;?ZV]:ZG<Z:B;>>=f?L:ZNJJFcaB?:m;Cp::Eg:B?:]:;[ZTZJJJ::]:;;[TJw:B<Z]:FBL:ZJJJ;V>B<Z]:BB<:;?kKmCQKG::B?>>=f?L;=:_::;]:<wb::;<;;?:M>:_::;<:;?kT]FJKG::B=ZaB@NN=::];[F:B;kk:E>>:[R:B;NN=;:L:cCm:?pJ:ZFJJZBQB@:L:[:JJ:kZ:E:]:Z:JJXRbB=:L:;C:Wm?N:qS^:F:]:Z:JJ;JQJyzzzzYJVZFJJYJbV;GkVqB:<ZJJJ;gPB;:L;ZBJJJcaB::<;;CkT]JJJ<JbTcB::f;]:;[ZT]F:B;>N:Z::>I:]:ZB:D<kJJ;JAB<Z]:BBL:;x[WmS^:F:]:Z>JJ;JQJ;BbG<Z:B?:m:ZJJJ;:<:Jw:B<Z]:F>L:ZB:D<ZJB:kZ:E>>:[F:B;gZBA:m:qCL:qB:=ZB:BA>N:o;m:;xrVo;<:ZFZWmC<:qB:<ZJ:B@cPB>g:B?>>=f?L;Z>:B>>N:A;<:;?kKmCQ=:gk<ZBJJJAZT_:k<E::B::L:ZBJ>A:k<fYJ:@wb::;L:;GJV?;]:?CQK>:>B;>N:e?L;Z:JB=F:JZF:B;o<::c=:::]:;?kT[J:B=>N:e?m:Z>JJ;cPB;:L;;GJV?pZ:ZNJJ;cPB?:]:;?kKZJJB<o::EkJ:E:]:[J:B:>m:EB:J<:>B=>o:lCL;?K:JZJJJJw>;:G:JZF:B;_]::V^:::m:Z>JD<ZkG<Z:B=>>>e?m:Z>JJJcPB;:<;;;lT]JJJIgN=:gk<ZJJJBw>;:;m:Z>JD<:kG<::B=>><e?m:Z>JJBcPB;g:B>>>;lK:JZF:B;Z]::R^:::m:;KZT[F:B;>>;e?L:E:<;;CJV>:>B=:L:j:::v:::ZFJJ<cPB=:L:;CZT[>k<ZJJJ;>Q=;kZ:EB:JZJJJZ_N=::L:cB::[BZ::C:J<:>B?>_:ftZ:;B::E:m:ZBZH:::B=:]:;;[Wu:Z:ZF:B<>><qd=:;:<;;GkT[J:B=>o:e?<<;;JB?F:JZN:BBcaB=:L:ZNZT]R:D<JZBA>><o;]:;[[WmS^:::m:ZR:D<ZZB<>><o;m;;[[WmS^:>:m:ZR:D<kZBA>><o;]:;[[WmS^:B:m:ZR:D<:[B<>><o;m;;[[WmS^:F:L;;;[T[N:B>>>>fC<;;;lLG:k<ZN:BBcPB=:L:ZNZT;GZT[>:=<g:B=:]:v:::ZJJJ<gPB>:m:;CZT[ZJJ:>L;=:>B?:<<eCm:Z>:B?caB@Z]:>Bm;;;;WZBJJJRbVqB::ZF:B@Z]:BB]:;;;WZVJJJRbVqB:;ZF:B@Z]:FBm;;;;WZBJJJRbVqB:<ZF:B@Z]:JB]:;;;WZVJJJRbVqB:=ZNJJJcPB?:<;;;lT]JJJKgN=:g:B?:<<e?m:Z>:B?cPJ<cPB;kJ:E:m:ZBZH:::B>>N:f?<;ZFJJ;cPBB>>:[Nk::;L;ZZZT]F:B;:L;eC];cB:;]VJJRJAB<>><qCbG<::B=:];cB:<]BJJRJABA>><qCbG<J:B=:];cB:=]VJJRJAB<>><qCbG<Z:B=:];cB:>]BJJRJABA>><qCbG<k:B?>>>e?L;ZJJJJgaB>>_>Ep::E:L;ZZZT[F:B;:L;e?N:e?L:E:<;;;lM>:>B=:L:h:::u:::ZF:B;Z<:;R=:;:m:Z>JD:NkG:N:B=:L:j:J=v:J=ZF:B;o<:Ic=:I:m:;;[T[F:B;>>>e?L:E:<;;[JV>:>B=:L:d:::r:::ZFJJBcPB=:L:;[ZT[>k<ZJJJ>>A;:;m:Z>:D::ZG:::B=>>;e?m:Z>JJ>cPB;g:B>>_:lK:JZF:B;w<::g=:::m:;CZT[F:B;>_:e?L:E:<;;?JV?pJ:E:m:Z>JE::ZH::k<Z:k<mEZ:<wQ:yC:JZBJKG::B::L:u:::Z::B<caB=>N:f;L:u:::ZBJJ=_N=::<:Z>ZH:B:B::L:u:J:ZFJJ=g@B;c=:::m:;CkTZ>ZH:::B<>o;Cp::Z::B;c=:=:m:;KkTZ>ZH:::B<>N<Cp::Z:JJ::<:f?o:lC<;eCm:Z>JJzAJV;?cZ>]:U]>ZG<::B=:]:ZJkT;lQV]JZT]BJJ>g@B;R^:::<;;`JLG::B=:L:qB:<ZF:B;R^:>:]:;[kTZ>ZG<::B<>>=f;L:qB::ZJJJS_N=::m:Z>ZG<Z;B=:L:qB:?ZF:B;R^:J:m:Z>ZG<k:B<>>>f;L:qB::ZBJJNg@B;R^:::]:;[lTZ>ZG<::B<>>Af;L:qB::ZJ:B=>>;l?>@mC<;fC]:;;LLG::B;oeJ;<BZ:<[Y[N:B=:<;e?L:=:>B;:L;rF:@Z>:B?Vn:J:L:ZNkG=Z:B;:L;rF::Z>JJZcPB;:]:;;mT]BJJYgN=:gk<Z:k<I;:B:>>Z:<;>BR^:::<:;;BZ>:;<qB:;Z:JJ:<B<JZZG<Z:B:>>Z:L:>BR^:Z:<:;;BZJ:;<qB:>Z:JJ:<B;JZZG<J;B:>>Z:]:>BR^:Rg:i;>k:z;<:;;ZG<k:B:Z]::>oz;>>Z:<;>Nc;B:Z]:B>L:Z::D<JJB<F:JZ>JJzS^:::]:ZFkH;::B<>_:e?]:Z>JJ>cPB;:m:;?ZT]FJJ:<c:Ap::E>>:[F:B:Z]:Z>>:;;BZ>:;?T:<:;;ZG<k;B:Z]:J>oz;>>Z:<;>Nc;B:Z]:R>L:Z::D<JKB<F:JZ>JJzS^:::]:ZFkH;::B<>_:e?]:Z>JJ>cPB;:m:;?ZT]FJJ:<c:Ap::EgkF::<:cB::Jk:B:Z]:>:;=Z::D<Z:>F:<:cB:>Jk:B:Z]:N:;=Z::D<Z;>F:<:cB:BJkk<]HJ:AwAB=:]:Z>JJ=cqM>wAB::L:ep<:<NJJ:g:B=Z]:F>N;o?>szAJVnC<;qB:=ZF:D<ZZBC:L:;xzz;>bBAB<<;C:We;<;qB::ZVJB>:m:cB::ZF:D<kZBD>_:odaB@Z]::BL;;xrK>:>B?:<;Z_:B?>_:od@D<::BDRo@[Jk<ZZJJ;JbBC:m:cB:;e;<;v>::ZR:BBR^::B:JZ>:B=Z]:ZB]<Z>JJ;gPJzzY:l?N:odqE;:ZB@>N:f?>:ZRJJ;g_B>g[B?cPJ;c@B<wN=::];;?:B>gKB>:L:;xrVZBZT[N:B::L:eC];h:::[:k::C:JZJJJ;gPJyzz:Ep::Z::B>:];ep<::VN=::L:ZJJJ;caB>c@B<_N=;kZ:Eg:B>>N:f?L;E:m:ZF:D<k:B?>Nz;cPJzAJVnC<:qB:AZF:D<Z[B<:<<;C:We;<:qB::Z_:BDc@B?gM:::m:cB:>ZF:D<k[B;>_:odaB:Z]::B<;;xrK>:>B>:m;ZB:B>>_:od@D<::B;Ro@[Vk<Z::BBR^:::m:cB:?ZZJJ;JbTZVkH;:k<rXZ:Jwr:wG<:;;R:fCm<_:Z::;<:cB:<]NJJDw>;:;L;JZZBHJ:JZsJJ::L;JJ[@E:]=?pJ:Z>JJ;gPBK:m<;[ZV[BKJ;>m==:_::;m=Z>lLG:Z::;L;ZcJJ;caBFgaB;>ozAo>;:K_uF<THStMD@?L?ZkZBC:L;IpJ:Z_JB;F:JZ::BD:L:Jc;B::L:ZN:>T:eBN:L?ZJ;BOFaBAgKBO:L:Z_:BAgKBC:L;Z>JJ;caB;VN=:g:=;g:KenBct_`IdRNBN:<=]_JBB:L;[Vk::;<?[N;BB:L:;dZU]oZT[>:BE>];;;JB>F:JZgJJJ=ZTZJZTZ>ZG<::B@:<:Zc:B;:[@Z::B;:L;Jc;ctF::Z>:BGcPB;:];;[ZT[R:B>>>;eC<;;KmKG:k<;?JB;>>:[J:BEgm::><?ZBKB@F:JZRkD=:ZBP:<?ZJ;BPJaBGgKBN:L:ZJ:BGgKB>:];;[ZT[R:B;>N:eCL:;`kKG:k<ZJ;BOJP=;B:JZJ:;:;<;;C:WZgZTcB:M[Z:B>>><@pJ:E:<;;C:WZgZTcB:O[Vk<ZgJJJ=ZTZJJJ<JbTcB::[_:BN>L?ZV:BBgaB;>N<Ep::EgZ::;L<ZkZKZ::BD:L;Jc;BOFaV?;L;Z_kKlO>;:;]<ZsZT;?ZH:::B<Z]::><<F>k<<:>B=Z]::BL:Z>JJ;gPV>:>B<Z]::><<F>k<<wAB;N>;:?>;JZJBB>>:F>k<ZB:D<::B;>o:o;K=[Z:B=Z]::gJB;:]:ZZZG<:k<ZZ:B;>_:od@BCR^::>N:[>:B=:m:cB::]JJJ;caBAR^::>N:ZV:BA>N:GhKBA:<<;KkT[RZ::G:JZ>:BARN=;:L:;?ZT[>:B@>>;eC];cB::Z_JMG:k<Z>JJ<gaBA:<;CK:JZZ:B>>_:odPJ>gPB;F:JZ>:B;Z]::R^:>:L:;KkT[>:B>>N:fC<;ZVkLG:k<E:];Z_ZG<:k<ZwJJ;cPBIg:BB>>;f?m>Z::D<ZJJ;gPB>:m:cB::[oJJ;><>;;JBB:L;[V:BD><=;;JB;F:JZF;B;>_:odPB@B:J<:o::;L:>wAB@Z]::NJJ:gJBD:<;[N:B;:L=AK:JZR:D<JJB?g:B;>N:e?L:Zc:BHcPE::JK>:>B?:]<fCL<ZZkLGBk<ZRJJ>cPB@:L:ZoJMG:k<Z:K==:m;ZkkT[_:BA>L;ZkJBDkJ:E>>:[:;B?>m;ZcJBF:L<[Z:B;:L=GpJ:Eg:BC>N<EpJ:Eg:BH:;=E:m<;;R:eK<:IgJJz;l::g:X<J:Y;wrB:>>ZDgaBAJ<:Z::B;:]:;;:>]BL;[F:B:Z]:B>>uAo>;:;<:Z>:B<>N:JBLB=g:B::L:ZB:BA>>Z;c@BA:lC[J:BA>>ZDc@C::L;ZF:B>J>::::::::<:;eB>:m:ZJkRUB];ZJ:B?Jq@ZR:B=:L;_hl<^LJ:AwrB:>>_>gaBAJ<:<:>B<:m:@p::ZN:>P:L;Z>:B<>>Z:DkT]RJJ::]:ZRkMUB];ep<:::L;cB:=;O:W;;yz;>rV]_ZG<k:B=:];;?ZT]ckL>:>B?:L:ZcZTh:::Z_JJ?JQJZzY:lGbG<kk<ZB:B@g>;:?>:;;BZ<:]:ZBJJ:<c:IhlT[Rk::;L:ZB:B@c@B=:L;JZ;B@>N:eC];G:k<E:]:ZFkMG:JJ:>L<;;JBDF:JZ>:B<:m:ZN:>R:<:ZN:B;:]:ZFJJ<D:BA:m;;lB;e;m;;tB;e;[AZVkE;sB;]R:BAwL:FLZBF>>ZBgoT[gZ::C:J<:>BBJ:JZc:BC>>ZBcoT;?ZTZg:L>:>B;:]:;?kT]ZZTh:::;;:BB:<;Jw;BE>o:BpZ:;?JBB:];;Cc:IpZ:FFk<ZV:BCgM:FL:BA:]<v>Z^>:]<;xzz=>bB@:L<;xzz=>AB<>N:f;<;JwKJ:><<ZRJJ=_N==:];;CkT[Rk::;L:ZBJJ;caB<:m:ZN:>R:];;?kT]RJ=:g:==g:BE>o:Bp::;?JBB:];;Cc:CpJ:E:m<;C:M>:>BA>N:v>Z^>>>:[Z:B;:]:ep<::>>:ZB:B>:lAFBk<ZR:BF:]:ZJ:>Y>>:[Z:B@>_:CpJ:ZRJJ;gPB@F:JZ>:B<>N:eC]:ZF:B?:;@ZRJJ;gaB@o::EkJ:E:];[c:BF>L<E:]:;?ZT]B:B=_N=:gk<ZVJJJLZT_:k<n::B:Z]:::;=Z::D<J:>F:<:cB:>Jk:B:Z]:N:;=Z::D<Z;>F:<:cB:AJk:B:Z]:Z:;=EcR:;NkYZ>:B:>N:oCm:JZZB<R^:::L:ZF:>BBL;qB:;Z>:B:>>@gC<;JZZB@R^:B:];>:_::;<:?p::=:>B<>N:v>::ZBJJ<cPB<:<:;?kT]:J=:g:B?>>:ZF:>Nc;B>NN=:>>:[Bk::;L:cB:<ZBZT;;ZH:::B>:]:;?ZT]BkKG:k<Ewk<;?:>=:k<sfJ:KwAB=:<:cB:<f?L=Z>:D<kJBI:L:cB:;[B;B;Z]::>m><:_::;<:cB:;]cJKG::BG>N:oCL<Zc:D<:ZTj>::]::;:;]<cB:;Z_ZTj>::?pJ:E:m;<wb::C:JZNJJ<DZKZ::B?o_V?K:JZRJKGJZ:zC:JZc:D<ZZBC:L=;[;U]kZT]gJE:>J=::m<h:Z:G:JJ:kJ:E:m<h:J?;GZTE:L;Cp:;F>k<ZRJKG>:BG>>@g?<=Zc:D<ZJBCgZ:zC:JZ_:BFcaBCo<:;o::Z_JE:BJ=:>>:F>k<Z_JE:NKJ=cq<Z:JLGF:BB:L;Z::B::L;Eh[B:gM:::<:;xzz=>QB:BkY<:>BDZ]:B:L=;[;UeCL:h:J:G::B;o<:<o::;;:=;g:B;o<:O>o:ehJB?B:JZ:JJ=_N=:>>:[BJJ:><:=:>B;:]:;G:UeCm:h:::]JJJ=caBA:<:IK:JZFkE:>JBC:];Z:JJ;JbT[F:B>:<:f?>;e?<:=:>B=:L<v>::ZFJJ<cPB=:<:;?kT]:J=:gk<ZN:BARN=;:<;;KZT[::B<>N:eC]:;[kKG:k<E:];ZZkE;:JJ;JbTF>k<ZZ:B?:<:Z::B?go@v>::Zc:D<J:BG>N:odq<j>::v>::Ig:B?><:E:<;ZFkT]_JJ<o>;:;<<;;kH;::BA>>:v>::Ig:B=>ozzAJV[R;BM:m=;C:WexL::BL;ZB;B?>N:odqE;:ZBFgaBE:m<;;BZ<c@B?:<=EhKBE:]:ZFZT]:;BC:<:Z::B=c@B>go@]>[T]wJJBgPBQ>>Z:=JBN>N:[_k::C:JZgJJzzY:EK:JZ_JB?kJ:EB:J<:>BENN=::]:ZF:BEgaBHcPB?:<;Z_JJzzz:lCL?ZFZTEK:JZ:;BOcPE:::B?:L?ep<::VN=;gZ::;m?<wb::;L:cB:B]_;BP>N:odqE;:ZB:>o:Cp::Z:KE:::B?o<::VN=::L;Z>;B::L@ZsJJzzY:l?N:odqE;:ZBH:<:ZsJLUB<:Z::BKgo@;xzz=>bB:cPB?:<:Z:[TF>k<Z:l<]:JMG:k::;<:cB::ZN:D<:kKG>:B?>>;e?L;Z:JJ>caB::m?Cp::EgZ::;<:ZwkK>:o::;<:h:::ZNJE::kKGB:B?>N:e?L;Z:JJ;caB::m=Ap::Eg:BI><:E:L?Z::BJgaB?>ozzIJV]skMG::B@J:JZ_JJ;cPB:F:JZR:B:>ozzIJV;?:We;m<v>::Z:JJ;caB:>ozzIJVZsJMG:k<E:m<[Z;BH:L>IpZ:F>k<Z_JB?gZ::;m>Z>:D<:[B:RN=::L:cB:BZRKJ;JbTj>::ZNJJzzz:lhN=::L:cB:AZ>:D<Z;BF>ozzIJV;C:We[]::VN=::L:cB:?[B;B:>m>E:<=;xzz=>bB::]>Z:JJ;JbTj>::]kZKG::B::<=fCL<Z_JJ:<c:e;<:ZkkLU:m<e?m<ZJKJ;g>B?>L<ZJKJ;gPBNoJ:Eg:B@N>BK>_Z<V_V<wQJ::]<?p::T>N:ZoJJ;JbB::]<cB::exL::NN=:c;B::]<cB:;exL::>>:Ah:BDN_VmO>;:;L=;?:W]::BDZ]:>cPJ::<@ZNJJzzz:lC]:;GJL]>k@v>::Zc:D<::B:cPJ::L;Z>k@v>::;;JB;B:JZBJJ=_N=::]<cB:<ZoJJRkaT[_:B@>_;e?m:;GJB:>>:[Jk::C:JZ::B<V>;:;m:j>::ZFkE;BZKG>k<Z_:B>>o:gdaB;:<:;GkTu:::Z>:B=o<::c=:;:L:ZFJE:>ZH:B:B:>L:ZJJJ;caB>>o;EpZ:E:m:;CZT[F:B:>N:eC<:ZBJMG:k<ZJJJAgN=::L<Z>JJ=gaH:Nl<E:m;ZZlH;::BB:L;v>::EgF<;VkYZF:D<ZZB?>>c<:bBDN>;:;L;;G:W[_:B?>_:o?];ZNJB>F:J<:>B>:<;;?kTlK:JZF:D<kKBAkJ:E:m:<wAB>N>;:?>;JZ:=;g:B=Z]:V:<<Z_ZTJok<]VZG<kl<ZR:BAcaBA:L;>wABA>>ZCg@D<:J;;;k<qB::ZRJJ>cPB@:<;;?ZT[J:BB>><eC<<;;c>Ap::Eg:B?>oAlO>;:;L;;G:W[_:B?>_:o?];;;JBB:L;[Jk::C:JZJ:B>>N:f?A;:;m:cB:B[V:=;g:B=BkYZJJK>:NJ>:;<F>k<ZF:D<:<BB:L<e;K=EBm;qB:BE:];ZVZT]V:B?JkYZVJJ:@kTcB::?>>:ER^:::];;KZT[R:B>>N:e?<;ZZJJBcaBB>>Z<VN=:gk<<:>B=Z]:BB<;ZJJJ;gPV>:>B=Z]::>];F>k<<wAB>N>;:?_:JZJB@>>:F>k<ZF:D<::B>>_:o;K=[R:B=Z]:BgJB>:m:ZRZG<:k<ZF:B?R^:B:];ZJJJ;JbTZ:kH;:Z::;L;dC];;?JM>:_:z;L;?K:J;C:>BB<;Z>kH;::B=:<;qB:;;K:>B><;;;:=;g:B=:m:cB:;ZNJJ<JA>GB<;qB:;ZF:D<ZJBA:m:ZNZG<Z:B>:m;;?:We;L:v>::ZF:D<:;B?>o:o;K=[J:B=Z]:BgJBA:m:ZJZG<:;=;g:B=Z]:>:L;;?:We;L:v>::ZF:D<:KB>:L;[Vk<ZN:BDgPBB:<;ZVJJ<JbTZBZG<::B=:L;qB:<<wAB;N>;:C:JZRJJ;o>;:CkYZNJK>:NJ<:;<]>:B:gM:::m:Z>ZG<JKJ<:;<[JJJ:kJ:E:m:ZF:D<J;B?>_:oCL:JoZB<R^:N:m:cB:<[J:B=:L;qB:<ZB:B>>N:od@B:gM:::m:cB:@Z>:>G><;ZF:D<Zk<[N:B=:<;qB:@F>k<ZF:D<J;B?>N:od@B:gM:::m:cB:@[Jk<ZF:B?>N:eS^:B:<;ZNJJ;JbT;;kH;::B=Z]:V:<<;C:We;<:;C:WelJ:EB:J<wb::;];;?JM>:>B?oJ:;C:>B><;;;:=<g:B=:L;qB:<ZF:D<J;B?>N:od@B:>_:o?>wUc@D<:ZBAgM::kZ:E:m:cB:?ZNJJ<JA>G><;ZF:D<Zk<[B:B=:<;qB:?ZF:B?R^:B:<;ZBJJ;JbTZ:JJ<JQJkj[TcB::]VkH;::B=BkYZNJK>:NJ<:;<F>k<ZF:D<Z;B?>_:o;K=ER^:Rg:B;>N:fC<:[R:B;>N;CCL:?K:JZ::B:VPJYFbB<>N:fSQJ;>AB<>N:oCQB@g:B=:m:cB:<]BJJ;caG<Z:B=Z]:R:]:;?:We;];v>::ZF:D<k;BB>_:od@BA>_:odaB<:]:cB::;?ZTqB::ZF:D<:<B?>>Rl?_:od@B;JkYZ:J;Z::B:VPJYFbB:>N:fSQJ;>AB:>N:oCr<;C:WehZB::<:cB::;?ZTqB::EV;:Z::D<::>F:<:cB:;Jk:B:Z]:B:;=EsE;;ckY<:_::;<:;;ZM>:>B:NN=<:L:;KkT[_:B:>_:o?]:=:>B;:]:e?>;f[]::N>;:;]:;KkT[B:B:>N:fC<:G>:=>gk<ZB:>B>]<Z:JJ:kN=;:]<;;:B<:;?]gJJ>gPB?:L:cB::]VJB@F:J<:_::;<:ZFZK]ZJK>:>B;:m:;C:We[]:::];@pJ:E:];?;<;;KZLlO>B@N>B>>o;BCAB>>>:FCQV?K:JZNJB<F:JZBJJ;R^:::]:;KkT[B:B>>N:fC<;G:k<E>N:[J:BBoJ:Z>:B=>_:od@D<:JB@kJ:E:<;;?ZT[Jk<ZNJJ>cPB?:m:;?ZT[F:BBNN=:g:B:>o:f?];;;JB<>>:[JJJ:>L;=:_:zCkY<:>B::L;@C<<G::BE:L;;C:W]FZTcB::G::B;:m:e[]::Bm:ZVkTZV:B=g@B=:m;EhKJ=gN=::<;;?ZTF>k<ZJJJ=kNJ::<;;GkKZBZVUN>;:?N:ZB:B>>N:qd@B>saB=:m:;?:MU>>:ZBk@[F:BC>]:=:>B<:m:qB::ZBJJ>gPB<:<;;?kT]JJ=:gk<<wAB?:];BK:JZ>:B?>_:odaB<Z]:::]:cB:;e;]:cB:<e;]:cB:=e?_:e?_:qlJ:E>>:Z::B?kN=:c;B;:L;;C:We[]::gJBA:<<>:NJ;><;;;:=<g:B;:L;;C:We[]::>m:;;JB<>N:E><;ZB:B=cq<[B:BC>>;e?L<Z::B?V>B?>N:e?L;G:k<F>k<Z:JJ<JA>B>]<E:]<Jkk<E_d:<FkY;krB:>>ZNgaB>J<:<kb:wC:J<:_::;m:HB::;Bk<;;JB=:L:ZBkK>:>B:Z]:JBm:ZBJJ;gaB<>_:od@D<:JB?>N:[R:B<>N:oC]:Z::D<JZTj>::>wAB<:<:cB::exL::N:B@g:B?c@B=:L:;C:We[]::gPB=g:B=>ozzIZU]::B=:<:;xzz=kqKe?>Dg;m:;G:We[E==g:B>>>Z;caB=:<;JF]:z;]:Z>JJZUZTCK:JZF:B>:<:Z>:B<:;CF>k<Z::B;:]:ZJJJ:iZT]F:B>>>ZDcaB?:KCZF:B?:<;;;R:e;<;Z::B;:]:JRm<smJ:E:<:Z>:B<:<;;;R:e;<;JVm<>;:::::::Z:JZi:B>>>ZNc@C:gJf;>Z:zG:JZ::B<cPJBR^:::]:;KZT]BJJ:MkKG:k<Z:JJ:MZT[FJJ:>]:=:>B<:m:e?N<qB::ZBJJ>caB<>>k=VN=:g:B:>>ZBcPB=>>:[Bk::;]:ZFZT;WZG<::B<>>;eC]:;;H:Ap::E:<:;;H<e?<:;;JB<F:JZ::B<cPJBR^:::]:;KZT]BJJZVN=:gJJ:>]:=:>B;:]:e?N;qB::ZBJJ>caB<>>Z;VN=:gk<fAJ:>wAB=:<;CK:JZJ:B=gPBA:m:;?:W]J:B<Z]:>cPB=:]:cB::ZJZT[Jk::;<;j>::[BZ::;m:j>::]RJK>:>B::]:;C:We[]::>]:F>k<ZBJJ<JQJkj[TcB::[Z:B@>N:f?]:Z>:B@>N;IKkYZB:B<VPJYFbB<>N:fSQJ;>AB<>N:oCQ;ZBk<;C:W]RZTcB::Z::BB>_:oC]:e[]:::L;ed@B<>>:]NZT;KkTcB::e?]:ZNJJ:aZTZRZTcB::[Nk<ZFJJ<cPB=:<;;CZT[J:B<:L;e?L;ZVJJ;gaBAo::Eg:B:Z]::]:B?cq<mDJ:>wrB:>>ZDgaB@J<:<:>B<:L:;;i;e`>;:;m:;;JJ:a:>N>m:ZJJJ:>>Z;:;?[J:B;:]:IpJ:Z>JJ;JQB?:]:Z>kT[>:B:Z]:>>]:Z::D<JKBBF:JZF:B?:<<exL::>_:odaBA:m;cB::;?ZTqB::ZB:B?cqE;::;:;<;Z::D<Z;B?cqE;:JJ<JbT]V:BAZ]::>N:eS^::g:B?>_:e?L;Z>JJ;gaB;o::EkJ:E:<:ZBJJ;g@B=:<;JV=B;NN=::<:Z>JJ;g@B@>>Z;c@B@:lG=:>B=:L;eC<:Z::D<::B@>>Z;c@B?c@D<:kTqB::ZNJJ>caB?>>ZCVN=:gJJ:>L;=:>B>:L;eC<:Z::D<::B?:];e[]::gaG<::B?>>;eCL;;;R:Ap::Eg:B@>>ZDc@C:gZm;::B@:L;;;i;ex>;:?>:[RJJ:>L;=:>B::];e[]:::]:ZRZTcB::g;L;e?L;ZRJJ>caB@>>ZBVN=:g:B<>>[BcPB>:<:;KB<e?<:;;JB@F:JZ::B@c@D<::B>:];e[]:::];;;B<e[]::c@UZNZT[N:B@>>;eC];;KI:Ap::E>>:[Rk::;L:ZRZTcB::ZF:B@c@D<::B@>>ZCc@D<:ZTg;L;e?L;ZRJJ>caB@>>y:VN=:g:B<Z]::]:B?cq=E:]:ZF:B>:L;ZR:>_gJy;BZ;zC:Y^:JJ:ulT]N:C::<:Z>:B<:L;;;R:eC<<ZN:>`:L;;?ZG<:R<ZZJJZDJJI:m:JZ<B?>>B;x:B>:;DZJ:>d:m:ZJ:>e>L<ZZ:B?:m:ZJ:B::L:ZB:>a>]<ZNJJ:P[T]V:BB>>ZC:l>T:L;;;B?eC];ZNJJ:@:>McKJZD:BA:KB;;<B@:KBZVJJZDJJI:L;;;s<eCm;JZ<B@>>B;x:B?>>ZDcaB@:;DZR:>d:m;ZR:>eZEBB:L;ZV:B@:<:Z>:B<:[Cs<eBF:L<s<]<s<eBEF@;:;m:ZVJJ:a:>Mc;B>:];;;R:JF[@ZkJBEg:B?>>ZXc@C::m<Ecj;;ZkY^:JJJ=kT]_:C::L:;l:UJZJB@:L:;;ZL>:>B=>>:Z>JJ<JA>Ncl::;<:cB::]Z:;:;];ZNJJFkaT]V:B>R^:B:m;ZZZG<::B?>N:e?L;E:<:;KZT[::B;:<;;?ZT]JkKG:k<EB:J<:NJ;:]:o;L;Bp::<:_::C:J<:_::C:J<:>B?sk:=>Z::gZ::;L;;;ZL>:>B@><:ZNJB>F:JZ::D<:ZB;>>Z:<B;IpJ<Z::B:Z]:B:L:;`:WmS^:::<:;lZT[::B>>N:fC<;G:k<ZR:B?:K>ZNJJ:kN=;:];[::B?><;=:>B::<:cB::;`ZWqB::Z:JJFcPB::<;;?kT]JJ=:g:=;g:B@:L;J>l<ZN:B<:L;;?kTZB:B?Zo@]>:U;[;UJZJBA:L:;G:WJZJB<:m;<;BZ:<B>rB:;ZVZJ:<BZ:<mG<:;BA:];cB::qB::ZV:B@Z]:FR^:F:L<ZVJJRcaG<k:B;>>:DK:JZVJJFcPBB:]:[::B;><;=:>B::m;qB::Z::BBR^:>:<:;[ZT[::B>>N:fC<;G:k<E:L;;CZLGF:B;>N:f?<;FJk<ZF:B@Z]:B>_:odPJ;R^::kJ:E:m:ZR:D<ZJJ<JbT]::B:Z]::>N:eS^:::m:ZR:D<JKJ<JbT]::B:Z]::>N:eS^::g:B@:;=FBk<;?:B?>N:o?N;fC<:Z:JJ;ko@[::B;>N:f?<;=:>B<:];ZN:BC>>=e;<;J_=B:>N:fC<:G:k<E:]:ZJJJ=JbT]:JJ>g@D<:JB>B:J<:>B?:<:cB:;]>:D<ZZBBkN=::];ZZJJFkaTcB::ZJ:D<::B:>><f[]::Z]::cqMG::B::L<cB:=]:ZG<J:B::<<;?ZTqB:<Z>:D<JJB>kJ:E:L:[:k<Z::B>R^:>:L<;;[T;;JJ:=:>Nc;BC>>n:cPB>>>>[ck::;<;Z::D<ZZG<::B>>>;f?<;ZcJJ;gPBD:<:cB:;]:J=:gJJI>L:ZcJJIo>;:;];;KkT[g:BCZ]:F?<;;?JB?F:JZ_JJJc@B;>N:fCL:;C:We[]::B<<ZJ:L>:>BE:<;;l:Ue?<:=:>B=:<:cB::;C:We;L;qB::Z:JJFgPB::<;;?kT]J:BBcN=:g:BB><;E:L;;?ZT[N:B;:]<Ip::Eg:B<:;=ZR:>F:m;Jkk<F>k<ZR:>Fg:BC>>o:c@C:gJO;BkY<:o::;]:Z::B;c@D<:JJ:V_T]BJJ;gN=;:L:;KZT]>JJs=kKG:k<<:>B<N>;:;<:;?ZG<J:=;g:B::<:cB::;;kK;C:We?<:E:<:;?ZG<:k<EwM:=wr::;m:Z::B;:]:;?JVZBJJ<>AB<>>;l?>:;;JJ::;F]J:B=>N:f;<;ChKB=:]:;?ZT]BJJBVN=:g:B=g:eD>J;zG<:;;B?fC<<_:Z::;<:?K:J;;JB::m:ZJkK>:>B<Z]:JB<:ZJJJ;gaB>>_:od@D<:JBC:<;;?:W]J:B<Z]:>cqE;::;z;<;ZB:D<:ZTj>::?>N:E:L<e;<:ZFJJ<JbTcB::]:kT[_k<ZB:D<kJBF:<:Z_ZT[J:B;>>:A?m<=:>BE>ozzI:B>:<:f;<:;xzz=c@B>oo@]cJJzzz:lCL<Z:ZT]>:B>wNVZN:B@:m;JoLJ::L;ZR:BA:KE;;:B?:];ZV:>h:L;;;ZH::Z::;m;cB::]B:B<>N:f?A;:;];cB::[F:=;gZ:z;]:?K:J;?:>B>m:;;:=;g:B@Z]:::]:;?:WJoJB=:m;cB::E>]:ZR:B=R^::g:B<:m:e;]<u:::ZV:BAZ]::Bm:;?ZT]BZG<:Z::;]:ZFJV>:>B@Z]::>m:F>k<<wAB<N>;:?N:JZJB=>>:F>k<ZR:D<::B<>N:o;K=[F:BAZ]::gJB<:];ZFZG<:k<ZB:B=c@BD>><qd=:::m;ZV:D<:ZB=>N:eC]:qB::<:>B<:m:lK:JZR:D<:JB=kJ:EBkYZBJK>:NJ;:;<[FJJ:kJ:E:];cB::ZBJJ;JA>G>m:ZV:D<:k<[B:B@:m:qB::E:]:ZFZTZ_JJzzz:nC]<u:::ZV:BAZ]::Bm:;?ZT]BZG<:Z::;]:ZFJV>:>B@Z]::>m:F>k<<wAB<N>;:?N:JZJB=>>:F>k<ZR:D<::B<>N:o;K=[F:BAZ]::gJB<:];ZFZG<:k<ZB:B=c@BD>><qd=:::m;ZV:D<:JJ;caB<R^:::L<>:>B::<=e?<:=:_::;]:ZBJJ;gPV>:>B@Z]::>m:F>k<<wAB<N>;:?N:JZJB=>>:F>k<ZR:D<::B<>N:o;K=[F:BAZ]::gJB<:];ZFZG<:k<ZB:B=c@B:o<::c=:::m;ZV:D<:JJ;caB<R^:::<:;?ZT[::BC>N:fCL<G:k<E:L:]::B>_N=:g:=;g:B;:L;ZR:BA:KEZ:JJ;>AB?:];ZV:>h:<:;?ZW;?JVZN:B@:m;Jo]::;<:;?ZK>:>BB>>ZEc@BB>>ZDc@>^kJ:E:]:ZF:B>:<<;;s<e;<<;;c<e;lCT>>:[:JJ:>L:=:>B;:<<;;s<eC]<ZZJJ:eZT]k:B:>N:l;<:;CJVZ:JJ>>QJ:>>:;;:>kBm<Z>JJ;g@BE__BEgKB;:L<Z::BEgKBC:<:;?ZT]:JJBVN=:g:BD:<=Z_JJ;>ABC>_:l;L<;KJVZN:B@:m;J:^@E:<<;;s<e?>c<>o=ZZJJ:@ZTJ>=BB>>ZDcPJZ>o=ZZ:>l:m:ZJJL>:>B=>N:o?<:ZJ:B=gPBCF:JZB:D<::B:cqE;:JB>B:JZB:D<J:B:cqE;:ZB=N>;:;<;;C:W]>:BB>>Z;caTcB::ZZJJ:iZTZ>ZTcB::ZN:B@:m;JB==;g:B=>N:f?L:ZJJJ<JbB>>>wUc@D<:JJ<JbBD:<<;;R:ed@D<::BB>>ZEc@BDc@D<::B?:];ZV:>m:<;;;h<e[]:::<;;;i>e[]:::L;ZR:BA:;EZZ:B=>N;CC]<>wAB;N:B;:L:b?oAnC<;;?kTq?N:l;<;;?:WmhJJ<JbB>c@D<::BB>>ZDc@B>c@D<::B?:];ZV:>m>>:[J:BDJkY;;J;ZFJJy;L:b?oAnCL:rdPJz;L:;?kT]J:W;xrVlh:B>:L;ZR:BA:;EE:<:;CZT[::BC>N:fCL<G:k<E:<<cB:ZC:<<cB:ZM:L;ZR:BA:[FE:<<;;B?eK<:E_V:;FkYZ>:;:;]:h:::[Nk::;<;cB::[R:B?>oz;>QK>:_::;];ZRJJ;gPV>:>B=Z]::>L;F>k<<wAB@N>;:?N:JZJB?>>:F>k<ZF:D<::B@>N:o;K=[N:B>Z]::gJB@:m:ZNZG<:k<ZN:B@cPJ:c=:::<;ZJ:D<:JJ;caB@R^:::]:h:::[Nk<ZF:D<::B@cPJ;gaB@:];h:::Z::BARQJ;>AB?JbVu:::ZB:B<o<::>N:e?o;lCL;u:::Z>:BA>N:eCm;Ap::Egk<b@J:<wAB=Z]::><;Z>JE::ZB?N>;:C:JZJ:B>>N:f?A;:;]:cB::[N:=;gZ:z;<;?K:J;?:>B>L;;;:=;g:B<Z]:::<;;?:WJoJB?:m:cB::E><;ZB:B?R^::g:B>:L;e?>:u:::ZF:B=Z]::>N:eC<;qB::Z>JE::JB?g:B<Z]:::<;e?N:fC]:ZBJE:::B::L;oCbH:::B;:L:h:::;?ZT;WJVu:::E:?:Z>JK>:>K::::::::Z:>K::::::::]:>B:>>_;_o@Ig:B:>_:o?>sLc@D<::B;>N:fWPJYFQJ;gPJ::L:;OZMU>>=;p:B:>ow:_o@edqhEJB=<>lY=krB:>>_>gaBKJ<:;GJBFJ_uF<THStMD@?LA=:>BF>N:ZZ:BA>K::B]AZo;BW:]A_CL=U>LAZk:BH:L=U>]=ZkJJ;caBF>oZ<VN=:gJJ:>L=>dH^ZaMPy_]K[oKJByQBFF:J;G:BF>>uUc@D<:ZBI:<<ZVJ>;:ZBX:LAZo;BXJaBJgKBW:m=Zo:BJgKBG:<=;KZT]kJ=:g:B=:]:f?]>Zs:BG:<<ZVJ>;:JBXB:JZB:B=R_BMo::Zc:>P:]<Z>:B<>>Z:DkT]oJJ::]:ZokMUBL=ep<:::]<cB:=;O:W;;yz;>rV]wZG<k:B=:L=;?ZT]skL>:>BD:L:ZsZTh:::ZwJJ?JQJZzY:lGbG<kk<ZB:BGg>;:?>:;;BZ<:]:ZBJJ:<c:IhlT[kk::;L:ZB:BFc@B=:]<JZ;BF>N:eC<=G:k<E:]>;?kT;pQM>:>BE>>;e?<=ZBKBHF:JZkJJDQydDXZG<::BF>>;e?<=ZsJJ;gaBHo::Eg:B@>>:v>::ZgJJ:R^:::]:ZFkMG::BE>>=e?<?ZRJJ@cPBO>>:ZBkT[R;B<>oZ<cPBQ:m<ZBJJ<JrT[Z;B@:]:;?:Wf?>[>cPBS:L>;SZV[c;B<>m==:>B;:m=ZF:BD:;@Zw:B<gPBGB:JZw:BQo>BI>N[>c@B=w_VG::BDZ]:ZB]=ZwJJzzY:l?N:odqE;:JJ?LJLG::BH:m=;tYz;cPJzzY:l?N:odqE;:JJ=DJLG::BR:m=;C:We?<=Z_;BI>N:odPBG>>:[sJJ<DJJ;:<<ZVJ>;:JBWF:JZo:BHcPJ<DkH;::BF>>]Bc@BW:<=eB::v<ehsB::Z>:BI>N:eCm=ZF:BD:;@ZkJJ>cPBF:]=;CZT]sJJ>LkKG:k<Zw:BPcPBGg:B::]<Z>:BIB]=ZFJJ<D:BK:L>;lB;e;L>;tB;e;[A<:>B=:m=;?ZT]wJL>:>BE:L=;C:Wed]::gUBWkJ:E:L:ZsZTh:::;;:BB:m;K>::Zg:BG>_:odaD<:ki]o;c]w;BE:L=;?ZT]kJJ<JbT]:[D<:ki^ON=::<>Zw[hsB::ZR:BF>N:odPJ;gM::g:BKwL:HLZBF:m:ZskT]s:BF:]=Ch[BU>o:IK:JZs;BW:TBW:m<ZoJJ<JbBHcPBV>_:[k:BH:<?e?]=ZN;BG>N:odPBJ:]@[ok::;<=;?ZT[kZ::;LAZsZD<:kiap::Zk:BGwL:::<<ZVJ>;::BVc]::gEc]w;BHc]::gvR?p::Z:;BFgM:::]=Zw[hsB::E:]=;KZT[s:BJ>_:e?<>ZoJJ<cPBG:<=ZglKG:k<E:m:ZwkLG:k<E:<;cB::JkJJ:><>ZNJJ:R^:::<;;;ZG<:Z::;]>?K:J;;JBGkJ:E>>:[ok::C:JZo:BG>N:f?A;:;L=[k:=;gZ:z;L=?K:J;C:>B><>;;:=;g:BJ:L=;C:WJoJBJ:L;cB::E><=ZJ:BJR^::g:B?:<=;?ZT]oZG<::BJ:<=;?:WeC<=ZR:BL>N:odqE;:ZBAgM:::]>ZVkT]BK=:g:BG>_:Cp::ZoJJ;RQBI:<>[sk::;]=j>::[J:BH:<=j>::v>::Zk:B>gM:::]=;CZT[s:BF>_:f?<=ZwJJ;gaBIo::EgZ::;m>G::BD:[?Zc:B;:]:;;BZ<gaB>>>:ZB:B>wo@]JZTh:::Zc:D<kJJ?JQJZzY:lGbB?R^:F:m:ZJJJ;caB@g>;:;]<Z>:B@cPE:::B?>N;o?>szAJVnS^:Fg:B<:<;EK:J;;JJ:<c:ZB:B<>>Z:DkMUgPBFF:JZ>:B<:<=e;m:Zc:>R:<=;?ZT]kJ=:gk<ZoJKG:JJ:><<=:>BJ:<<;?:WexL::>m=Z>:B<:m:Zc:>RB:JZwJJ<o>;:;L:ZBZTh:::;;:B<:L<JwKJ;>m=F>k<Z::BD:L:ZB:B=:m=;;:BK>_^>c@BK:[AZw:BKwL:HL:B<:L<Jw;B<>N:e?<=ZwJJ;gPBHF:JZ>:BF:m:Zc:>R:<=;?ZT[k:BH>N:fC]=G:k<E:]:ZwZT[B:BB>N:eC<<ZokKG:k<E:L>;;C;eK<:EsX=;NlY^:JJkAkT]k:C::<=;;T:e?>:;lF:JJ[@Z:JJo^ZT[_JJW>]><:o::;L<cB::G>:BC>>;f?L<ZBKJ;gaBLo::E>>:[Bl<Z>JJo=ZT[_JJW><?<:o::;L<cB::G>:BC>>;f?L<ZJKJ;gaBNo::E>>:[Jl<<:>BL:<?eC]<;Cc:eCL??K:J;;JBCkJ:E:]>;?c:e?]?Z:JJ>cPBQ:m:ZJZV[Z;BD>NZ<cPBS:L:ZBKJ<JrT;;B<f?]@;;JBCF:JZ::BM>_:oC]<e;L:ZF;BPgPJ<JbTZF;BP_o@h:::[gZ::C:J<:_::C:JZB:;:;m<[>;=;g:BRNN=;>>:[>;BEoJ:E:m>;?ZT]g;BO_N=;:L>[gk<;?JBD:<=;;T:e;m<;C:WeC<>Z:;D<:JJ;caG<:JJ;><>F>k<Z_;BMgPBV:L?ZFlT[:;BD:m?e?m<Zc:BTcPBI>>:[cZ::G:JZg:BI:]<Zg[TZRKLUZ]:::L>@K:JZgJJ>cPBE:m=;KZT[w:BV:]<;?ZT]ckKG>:=<gk<ZcJJ;cPBJg:BF>>c;ca:zC:JZ>;BJ>o:CCQK>:_::;<;?;<>;hJLmO>;:;<=cB:u;>m=Z:KBDF:J;dR:Zc:BD>_];wo@[g:B@J:J<:>BC:L<;?kTlp::Z_JK>:NJ>:;<[o:=;g:BG:L<;G:WJoJBGg:BG:L<;C:We?_>qB::<:>BB:<<;?kTlp::ZZJK>:NJ>:;<[s:=;g:BH:<<;G:WJoJBHg:BC>N:e?L<Zs:BB>_:od@BE>o<fS^:::<<;?ZT[Zk<ZwJJ;cPBI:]<ZgkT]cJJDgN=:g:BF:m=qB:u;kJ:E:<>[ck<ZFJKZcJJ=__VG>:BFZ]:_AJBIF:J;d:BD:]<;dkMU>m<ZR:;:C:JZ_:BC>N:f?Q=::L<?K:J;K:>B>L=F>k<Zo:BC>o:o;K=[ok<Zo:BC>_:odPJKR^::B:JZZ:BB>N:f?Q=::<<?K:J;K:>B>]=F>k<Zs:BB>o:o;K=[sk<Z_JJ;cPBC:]=ZZJJ<JbTZgJJ=gaG<::BB>N:e?<<E:m=;?ZT[w:BD:m<fC]<;CkLG:k<Zk:BIR^:_AJJ:kZ:E:<>[ck<ZBJKZcJJ>__V?K:JZkJJZ@ZTZ>KJ<JbT]g:BEZ]::>N:eS^:::];>:_::;L<Z_JJ;gPVG::BCN>;:?>;JZJBGkJ:E:L=Z_JJ=JA>G>L=E:L=Z_JJ<JbTZ>[G<:Z::;<<ZZJJ;gPVG::BBN>;:?>;JZJBHkJ:E:]=ZZJJ=JA>G>]=E:L<;?ZT[_:BH:<<;C:We?>:qB::ZZJJ;cPBBg:BD>N:f?]<Zk:D<:X:[wk::?_;Zc:BD>_;IhKBE:];>:_::;L<Z_JJ;gPVG::BCN>;:?>;JZJBGkJ:E:L=Z_JJ=JA>G>L=E:L=Z_JJ<JbT;;[G<:Z::;<<ZZJJ;gPVG::BBN>;:?>;JZJBHkJ:E:]=ZZJJ=JA>G>]=E:L<;?ZT[_:BH:<<;C:We;m<;GkTqB::ZZJJ;cPBBg:BI>N:e?m=Zc:BEgaBD>_:Ep::E:<=ZwZG<:X:E:L>EBm<;C:WeCL>Z>;D<::BDcaG<::BDNN=;gk::;];>:_::;L<Z_JJ;gPVG::BCN>;:?>;JZJBGkJ:E:L=Z_JJ=JA>G>L=E:L=Z_JJ<JbTZgZG<:Z::;<<ZZJJ;gPVG::BBN>;:?>;JZJBHkJ:E:]=ZZJJ=JA>G>]=E:L<;?ZT[_:BH:<<;C:We?>:qB::ZZJJ;cPBBg:BD>N:fC]<G:k<E:<>ZF[T]F;BO_N=:gk<ZkJJZ@ZT;GKJA:<=;;G:eC<:JZ<B@J:JZ:JJM>o;Zk:>lgJJBfJBB>o=[cZ::G:JZkJJZ@ZTZZ:D<:JJ<JbTcB::G>:BB>>;f?<<ZcJJ;gaBDo::E>>:[ck<<:>B@NN=::]>;O:B?:];ZV:>g:<?;O:B?:];ZV:>g:]<;K:B?:];ZV:>g:]<;lrK>:>BD>_:o?<:;;QBBF:JZkJJJ=ZTZZJJJeZTcB::;C:We[]::>o:ZN:B@:m;Jk<B::<<;KZT]ZkKG:k<E:L<?p::ZoJBB:]=[gk::;<=ZZ:D<:JJ<JbB:c@D<::BF>>o:c@B:c@D<::B?:];ZV:>m:<<cB::;;lT]:JJ<o>;:;m<cB::Z:JJ<JQJwI]TcB::ZN:B@:m;Jkm<ZZJJ>cPBB:m<;KZT[g:BC>N:fCL<G:k<E:]<;G:U;d[T[ZJJ:>L<=:>BF>>c;c@BCc@D<::BF>>o:c@BCc@D<::UZZZT[Z:BC>>;eCL<;lF:Ap::E:<=cB:u;><:Zk:D<:X:Zk:D<JX:[B:BG:;=Zs:>F:<=;;Y:eK<:;?:WZZZTZBJJ=kaTZ:JJAkaTEwd:;RkYZBJJ<JQJ>caB>:;<[R:B>:;<[Z:B;J:JZFJJ::L:;C:WJJ[@E:];[NJJ:><;=:>B?>>:qB::ZNJJ>cPB?:<;;?ZT]J:B<oN=:g:B;J:JZ:JB?:L:[Jk::;];ZN:D<:JJ<JbT]V:BAZ]::>N:eS^:::L;;KZT[N:B>>N:fC<;G:k<E:];;;ZG<::B<J:JZZJJ>cPB?:];[JJJ;>m;=:>B?:<;cB::Z_ZT;?:W]_ZG<::B>>>;e?<;ZNJJ>cPB?:m;;?ZT]V:B<oN=:gk<Z>:;:G:JZ::D<:ZB<J:JZF:BB:]:;C:WeC<;cB::]BZG<::B>:]:;?ZTqB::E:<:;KZT[::B=>>;e?m:Z>JJ;gaB;o::Eg:B@:;=ZZ:>Fg:o;>Z:z;L:>:>B;>N:f?];ZBJE::JB?F:JZJ:D<:JB;:L;;xY:lO>;:C:JZ>:B;>N:f?A;:;m:cB::[N:=;gZ:z;L:?K:J;?:>B>L;;;:=;g:B=Z]:::L:;?:WJoJB?:<;cB::E>L:ZF:B?R^::g:B;:L;e?>:u:::ZJ:B>Z]::>N:eCL:qB::ZBJE::JB?g:B=Z]:::L:e?N:fCL:Z>JE:::B::];q?N:l;L;oCbH:::B<:]:h:::;?ZT;WJV]NZH:::B@>N:fC];;xrKG:k<Eg:q;>J;z;<:cB:<>:>B:Z]::>m:Z::D<JJB>F:JZFkE;:JB<:<;j>::]N:;z;L:ZBJJ<JQJkj[TcB::;C:WeC]:ZB:D<:JJ;caG<::B?>N:f?]:Z>:B?>N;IKkYZB:B<VPJYFbB<>N:fSQJ;>AB<>N:oCQ;ZBk<;C:We?>ZCcP;Z>:B<>_:odq<]B:B<Z]::>N:eS^:::<;;CZT[J:B=>_:e?m:ZRJJ;caB@:<:cB:<Cp::Eg:B;>N:qB:ZB:L:;;e:Z>JJ:eZTJN=B;>>ZCcPJZ:L:;;BAe;KGENR:;FkYZ>JK>:>B<:<:;G:We?>ZDcqD=:k=E:<:;C:W]:JJZE[TcB::[F:B:>>wUc@D<:JB>:L:;?kT[::B;>N;HK:JZ::B:VPJYFbB:>N:fCL;q?N:l;<:;?:Wm?<:E:]:ZJJJ=JbT;;c<ehm:::m:ZNZTr<DB<:<:;G:We?>ZVcqD=::cEsT:<B:Y=wAB;J:JZ:JB@:L:[Nk::;];cB::ZVZT[V:B@>>;e?];ZNJJ;gaB?o::E:m;Z>:BAg;iJB:K[AsD`WN?rz^c[Jk::;<;[F:B:Z]::BL;>:>B=:L;s<[:>?XZfNqKOVzj]<TB=g:B<J>:::::::::::m:ZF:Kl^qsBPEy_vESU:m:ZF:K::::::::::kRU_n:::<:;KZT[::B<>><e?]:Z>JJ;gaB;o::Egk<q@J:>wAB<>>:DK:JZ::D<:JB=:<:cB:;[J:B;>L;ZBJB@F:JZFJJzzz:l?>oF@:UZFJJJRbT]F:B>>ozzIJV;`Hc<k@B>>>>qdaB>>>>odPJ>RQJ=:QK>:>B?:L:ZFJJzzz:l?>oF@:UZFJJJRbT]F:B>>ozzIJV;`Hc<k@B>>>>qdaB>>>>od@B<:QJ<JbTcB::qB::E:L;;KZT[N:B@>N:fC];G:k<Z::B=R^:::<:ZJZG<Jk<Ewg:;NkYZ::D<k;B;:L:;;e:khaB?>_:odPB@F:JZB:B>c@B>:];e[]::R^:::<;;KZT]JJJ:akKG:k<<:>B;>N:eC];ZNJJZDZTIp::Z>JJ;JQJ<cPB?:L:;;e:i?>c<kPJYDZT[V:B;><;=:>B>>N:eC<;Z::D<ZkMG>:B<:<:cB:?ZNZTj>::;C:WeC<<ZZ:D<:JJ;gaG<::B?>_:e?L;ZJ:BAVN=:gk<Z>JJZ?QB<:<:cB:BZ>JJ<JQJ:yQVe?L;;;JB>F:JZF:B>c@B>:L;e[]::R^:::<;;KZT]JJJ:@kKG:k<<:>B@:]:;;]TIp::ZBJJYcPB<:L:;?:W;CZT[Jk::;L:;?ZT]>:B:Z]:BwN=;:<:cB:;ZJZTj>::>:>B=:<:cB:@ZJZTj>::;C:WeCL;ZN:D<:JJ;gaG<:k<ZJJJ<cPB>:L:ZBkKG:k<EgJ=::<:cB::Z>:D<:kTEwe:;VkY<:>B>N>B::<;;G:WeC<<cB:;cB:<]R:B<sNV?K:JZJJJ:VNBD:<<;KZT[N:B<:];D?L<ZJJJ;gPB>F:JZF:B=Z]::Bm;;lZTqB::ZN:D<:JBE:L;ZVZG<::B?>>;fCL;ZgZG<::BDN>;:;L:ZRJJFkaTcB::[_JJ:>L;FFk<ZJJBB:L;;[kT]JJJ>caB?Z]::B]<cB::ZJ:D<::D<:ZT[JZ::;L<;?JV?p::ZJ:B;:];;l:Ue[]::BL<Gp::Zg:D<JJB?kk:E:m;ZRZG<Z:BA:<;qB::ZV:BDR^:>:<:Z>:B<:m:ZZ:>t:<<;;kK[c:BB>N:f?<;ZN:D<::D<ZZB@:]:B?L<ZZ:B<:];DCQ=:gk<Ig:BA:L<qB::ZV:B?R^:>:m;ZRJJ;caG<Zk<^j[:owQ;wG<:;;mT]s:C::]=;;ZH:w;BH>>:qB:@ZsJJ:R^:N:]=;xZG<:;BHBN:rB:<Zs:B<R^:>F:JZsJJ>cPB[:m>;;F[xc@B;w_B<>mEZFKBO:L:ZFlT;;F[x:]:U:m>e?m>;;JBE>>:[oJJ:>m=;;JBAJ>::::::::::><H;;JBZ>>:[JlB:>>gDgaB=J<:ZFJJ:R^:g:m:;;ZG<Z<BHZ]:BJ:JZs:D<:KBBF<:;;H:fCm;_::BA>>:qB:MZVJJ:R^:B;m;;;oT<;kG<::BA:m>qB:QZV:BOB]:qB:PZV:B[R^:J;m;;;ZG<J?BA>>:qB:EZVJJ:R^:o:m;<;kG<J<BAB>:rB:HZV:B:R^:k:m;JN;B=>>:qB:EZFJJ:R^:c:m;;;G:e;<:ZB:BM:m;;K]T]R:BA:l@ZR:BB:m;;lF:e;m;;[F:e;K@<:>BAZ]:BCL<?K:JZV:D<kNB>kJ:E:m;cB:M[J:BAZ]:gB<=?p::ZV:D<JLBB:m;cB:D[wk::?N:[c:BIwL::J:JZZkE;:JBDg:B>:m:cB:D]NJJ<JbTcB::ZgZK>:_::;L;ZNJJ;gPV>:>B=Z]:g>];F>k<<wAB?N>;:?>;JZJB@>>:F>k<ZF:D<k<B?>o:o;K=[R:B=Z]:cgJB?:m:ZRZG<km<ZR:B?>_:od@B<R^:::m:ZF:D<ZLJ;caB@R^:c:];Z_ZKGBk<ZwJJ<cPBI:<<;CZT[Z:B<:]<e?]:Zk:BE>N:eCm<Ap::Eg:B>:;=ZVJJ_c@>V:m;JV;BA>>s:c@C::m:cB:D]wJJ<JA>B>m;ZF:D<kLBZg:BH>oAe?]?ZsJJRcPBQ:]=;K[T[Z;B=B>:rB:BZFJJ:R^:N:m:<;kG<J:B=>>:qB:=ZFZJ:V^:R:m:Z:ZG<:[::C:J=:>BO>]:Zo:;:;<?Z:]T;KkTcB::[Bk<ZFKBE:L=ZwkK>:>BN:<Be[]::>m<E:m:<;kG<JV:ZFZJ:V^:FA:B=:m<qB:IZF:B<R^:s:m:;;ZG<:V:ZFZJ:V^:k@:B=>>:qB:i;:m:Z:ZG<kU:ZF:B[R^:k:m:;l:>BBL@qB:GZg:B<gaBB:L@Jo;B=>>Fe?mCZs:D<JJBc:m:;;U:e?]<;;JB?J>::::::::::>mG^:JJZMH:fC<;_::BB>N:eC<<;?:WJZJB]:<;;;ZG<khPZJJJ:R^:Rf`::;<<;C:WJZZBdJ:JZBLKGF:B>BNZ:<BZZV^:B:<;;;CIeC<<;;JJ:u;>Nc;B>>>oT=ZT<;kG<::B>>>:qB:lT;<;<;kG<JfPZJJJ:R^:we?B>B>:rB:mT;<;Z:ZG<:gPZJJJJeG:eC];JN;Bb:<:ZB:BE:<;;Kfq:caBC:];Jg;BC:<<JF=Bc>>:DK:JZJJJJm[T[:=B>>>_DcPBl:<;;;S<e?]FZJJJJ]G:e?]>ZJJJJe^T[c<B>>>_@=ZT[c;B>>>_ccPBf:<;;;Sl:cPBV:<;;;tCe?<E>dH^ZaMPy_]K[_=B>>>mT=ZT[oLJz?];;;JBBF:JZVMBv:<;;Kfq:caB?:;AZo]J:V^:::LE<;kG<Z:B>>>:qB:lT;<;<;kG<JfPZJJJ:R^:we?B>:<:qB:kT;mCZ::B<:m<ZJJJVfG:e;<;;[hq:c@B]>>;ZJJJJm^T]F=B?:<;;;dq:c@Bd:lEZ_=B?>>:ZJ:D<kfPZJJJZjG:e;<;;;iq:c@>bJ>::::::::<:;eBrJ@;:;L;cB:<[g;BD:;AZcJJ:R^:B:L;cB:=[_:BDB>:rB:?ZcZJ:V^:V:]<Z_ZG<k:BD:L;cB:<]oKJ;JbBK:;<]_ZG<::BD:L>JZZBFR^:>:]<ZoKJ<JA>BB<>qB:>Zc:BK:;<]s[G<J;BD:L>JZZBKR^:R:]<ZgKJYDZT]JMJZDZU]NMJ:a:UJZZBYR^:V:]<ZgKJYcaBq>_:o?>Zz?A>BBm@qB:B<:>BCN>BFN_V?K:JZ:KKZsKKZ>KKmCABYN>BUN_VmO>;:;]<Zo[G<Z:B?Z]:BB]E>:>B?Z]:R>LAZN:D<JKB^:L;cB:>[J<B?Z]:>>LCZN:D<:JBaF:JZ_:BawL::gM:::<=ZNmE;:kH;::BX:mBj>::v>::Z:;B_Z]::R^:::L>ZolE;:kH;::Ba>_:e?]CZ_JJ<cPBC:LC;CZT[N<BF>_:e?<=ZJLJ>cPB_:<>;KZT[:;B^>_:e?mBZsKJ<cPBX:LA;CZT[o;BK>_:e?L>ZsLJ;gaBio::Eg:Bq>>Rl?<=ZJMJZDkM>:>Bp>>c<kPBJ:L;cB:A[_k::;mAZ_:D<:ZG<::BC>>;e?L<ZwKJ>cPBY:<>;?kT]:K=:gk<Zk:;:;L;cB:B[_k::;m@Z_:D<:ZG<::BC>>;e?L<ZgKJ>cPBU:<=;?kT]kJ=:gk<FBk<E>oYJF::E:<;;;dAe;mF;;R<JF[@Zk<BV>>Z;:l>T:mDZcKJ:D;>Mc;Be:]>;;c:JF[@ZVMBtg:B>>>>e;<;;;CIeCL;;;R<JF[@ZB=BV>>Z;:l>T:LFZcKJ:D;>Mc;Bk:]>;;c:JF[@ZNJJ:>>ZD:;?T:<;;Kfq:c@B?:lFZRJJzW>;:?>:[Nk::CkYZJJJJc@B?c@D<::i>;::::::::yI]<<;;;CIe;L;eCL<cB::s<eBuJ>::::::::w;G@BuJ>::::::::::RPV>:>BugD=;gJJ:gJBF:L<ZkZG<::B?>>;eCL;;;R<Ap::E>>ZC>L;=:_:z;<;;;[TZNZTcB::sL>::::::::szBDB>>>_wc@B?caBCZ]::ZEc]c=K::::::::kAoRZc=K::::::::::ZSlK:JZcneF>k<;;k<[k:BC:<=qB::ZNJJ>caB?>>ZDVN=:g:B>>N:qB:_>;<;;;CIe?>c<:]@JN=BV>>BZB;>pg:BB>_;C;mGZg^RmO>;:;<;;;CIeC];ZJJJJu[T;;R<JF[@Zk;Bg>>Z;:l>[N:BT:mD;;c>JFKBC:]>ZcLJ:D:>M><=ZJJJBcaBJ:];;;e:JR=BJ:L;;;<>q:<;;?ZG<:CKZRJJZD:BC:KGZNJJZ:<=JN=BB>];E:<<;?ZT]Z:BcVN=:g:B>Z]:VfOB?g:B]:;=ZN:>F:LDJk:B>>>hT=ZTJk;B>>>_T=ZTJV;B>>>s_=ZT_::=;g:=<g:BD>>:ZF:D<ZU:JcKBr>>:[ZJJ:>];;;JBE:]<cB:<>:>B=>>;e?]:=:>BDZ]:::<<exL:::]<cB:;ZZZTj>::Zc:D<:;B@c@D<::B<:lAZZJJ<cPBB:];;KZT[R:BE>N:eCm<Zc:D<ZJLG:k<E:L=ZwJL>:>BA:<?e;m:cB:=qB::E:L@>:>BS:;BZ_;>Fg:Bs:mGZ@<HZJKJ>cPBN:m:;;U:e;;AZoJJ;caBG:m=Gp::EB:J<:>BI>_:IK:JZs:D<ZJ=;g:B=Z]:F>L?F>k<;;JBG:m:;;ZG<:U:ZFJJ:R^:k:m:;KZTZs:D<:;B=>>g;c@B=>>Fe;K@ZF:D<:U:]JJJ>gPB<J>::::::::::>mGZF:D<kJBO:m:cB:F[Rk::?>:[g:BGJ:JZB:D<:JBEg:BO><<ZR:BGV>;:;]:cB:;[Zk<ZBJJ>cPB<:mGZFJJ>c@BE:<<Jc;c[V=BG>N:eCL=ZRJMG:k<ZV=BsF@;:;m;Jk:B=:];qB:DZRJBI:<;[V:=;g:B>:;=E:m;;KkT[Z:B=Z]:>><AZF:D<ZJBX:m:cB:?[:;B=Z]:J><?ZNKJcYJL[>KJ:>m<=:NJ:>L=Zg:;:;<<cB::[ok<ZNKB<:m<ZwZK]RJK>:>BBZ]:>>]:E:];ZwLV[J:B=>>;eC];Zo:B<>>:JBLBr:];Zo:B<>N:JBLBs:];Zo:B<:m:;;U:e;m:;;^TJVLBtB:JZB:BGR>;:;<;;?:BP:m?ZZ;>g>N:;C:BP:m?ZZ;>g>>:;W:BP:m?ZZ;>gkJ:E:m:<;kG<JV:ZFZJ:V^:FAJJ:>];ZFJJ:R^::A:B=B>:rB:g;:m:;;ZG<ZU:ZF:BNR^:w@Z::;L>ZZ=BtJ>::::::::<:;eBtJ_aSaSaSaSwzBTSmCmA?p::Z:;BG>_:od@D<:JBD:<>ZBJJ;gaB@>_:od@D<:JBC>N:[N:BX:];;?:W]RZTj>::>:>B@:<AexL::>L;E:m:ZcZG<Z=B=:LBqB:FZF:B?:L<eCL;qB:IZFJJF:;<]RZG<J=B?:]<fC<=ZR:>W:m:;;^T[_:B=>>g;cPBSF<:;;nT]R:C::<=;?ZT]BKJ;JA>B><=ZRJJ:R^:g:];;;ZG<Z]::;]>;C:WJZZBLJ:JZkJKGN:B@>>;eC]@JN;BC:L;qB:=Z_:BDR^:B:L<ZJ;BD:L;ZRJJgc@B@>>De;<=;GJJ::L@Zc;BL:lEZk:>F:];cB:EJk:BL:;=Zc;>Q:];;;^T_::=;g:=>g:BS>>:ZF:D<ZU:]RJJ;:[B[Z=B=Z]:oBL;?p::ZN:>Z:L;Jkk<<:>Br:<H^O>Br:LH^O_V?K:J;;:B>:m:;KZTZo:B<:]?ZV;BR:lDF>k<ZZ=BtF@;:;mA>:NJ;:<;ZFJJk@ZT;;:B@:]?ZV;BR:lDFBk<;?:B>:m:;KZTZo:B<:]?ZV;BR:lDF>k<;C:B>:m:;KZTZo:B<:]?ZV;BR:lDE:m:;;U:e;;AE:<<;KZT[Z:BE>N:eCm<ZwJMG:k<ZFJJ>c@>V:m;Jk:BZ:;=ZFJJkeZT_::=;gJJz;l::g:B;:m>Ep::E:]=cB:@Zs:D<J;>>:<:Jk:BHZ]:R:;=ZsJJZc@C:gk<cf;<:>>cBgJM;::::>::::J:::::;::::B::::Z:::::<::::B::::k:::::=::::F::::k:::::>::::J:::::;::::>::::N::::J;::::?::::N::;;S<ENP:::::;::::B::::Z:::::=::::F:::::;::::>::::N::::J;::::@::::R::::k;::::A::::Z:::::<::::C::::_::::Z<::::D::::g::::k<::::F::::k::::J=::::G:JJ:ek<C;;:::J>::::L::::::::::<::::A::::_::::Z;::::D::::N::::k<::::>::::k::::k:::::G::::B::::Z=::::;::::w::;;s<ENx;;:::::::::J::::::::::>::::::::::;:::::::::J:::::<::::F::::::::::;::::B::::k::::::::::>::::Z:::::=:::::::::J:::::<::::F::::::::::;::::B::::k:::::>::::N::::Z;::::A:::::::::J:::::<::::F:::::;::::?::::R::::k;:::::::::>::::Z:::::=::::J::::J;::::@::::V::::::::::;::::B::::k:::::>::::N::::Z;::::A:::::::::J:::::<::::F:::::;::::?::::R::::k;::::B::::_::::Z<::::E::::k::::J=::::H::::w::::::::::;::::B::::k:::::>::::N::::Z;::::A::::Z::::J<::::D::::g:::::=::::G::::s::::k=:::::::::>::::Z:::::=::::J::::J;::::@::::V:::::<::::C::::c::::k<::::F::::o::::Z=::::I:::::::::J:::::<::::F:::::;::::?::::R::::k;::::B::::_::::Z<::::E::::k::::J=::::H::::w::::::::::;::::B::::k:::::>::::N::::Z;::::A::::Z::::J<::::D::::g:::::=::::G::::s::::k=::::J::::>;:::Z>::::M::::J;:::J?::::P::::V;::::@::::S::::c;:::k@::::V::::o;:::ZA::::Y:::::::::J:::::<::::F:::::;::::?::::R::::k;::::B::::_::::Z<::::E::::k::::J=::::H::::w:::::>::::K::::B;:::k>::::N::::N;:::Z?::::Q::::Z;:::J@::::T::::g;::::A::::W::::s;:::kA:::::::::>::::Z:::::=::::J::::J;::::@::::V:::::<::::C::::c::::k<::::F::::o::::Z=::::I:::::;:::J>::::L::::F;::::?::::O::::R;:::k?::::R::::_;:::Z@::::U::::k;:::JA::::X::::w;:::::::::;::::B::::k:::::>::::N::::Z;::::A::::Z::::J<::::D::::g:::::=::::G::::s::::k=::::J::::>;:::Z>::::M::::J;:::J?::::P::::V;::::@::::S::::c;:::k@::::V::::o;:::ZA:>>^MgJqA>::::J:::::;::::>::::J:::::;::::>::::J:::::<::::B::::Z:::::<::::B::::Z:::::<::::B::::Z:::::<::::B::::Z:::::<::::B::::Z:::::<::::F::::k:::::=::::F::::k:::::=::::F::::k:::::=::::F::::k:::::=::::F::::k:::::=::::F::::k:::::=::::F::::k:::::=::::F::::k:::::=::::F::::k:::::=::::F::::k:::::=::::F::::k:::::>::::J:::::;::::>::::J:::::;::::>::::J:::::;::::>::::J:::::;::::>::::J:::::;::::>::::J:::::;::::>::::J:::::;::::>::::J:::::;::::>::::J:::::;::::>::::J:::::;::::>::::J:::::;::::>::::J:::::;::::>::::J:::::;::::>::::J:::::;::::>::::J:::::;::::>::::J:::::;::::>::::J:::::;::::>::::J:::::;::::>::::J:::::;::::>::::J:::::;::::>::::J:::::;::::>::::N::::J;::::?::::N::::J;::::?::::N::::J;::::?::::N::::J;::::?::::N::::J;::::?::::N::::J;::::?::::N::::J;::::?::::N::::J;::::?::::N::::J;::::?::::N::::J;::::?::::N::::J;::::?::::N::::J;::::?::::N::::J;::::?::::N::::J;::::?::::N::::J;::::?::::N::::J;::::?::::N::::J;::::?::::N::::J;::::?::::N::::J;::::?::::N::::J;::::?::::N::::J;::::?::::N::::J;::::?::::N::::J;::::?::::N::::J;::::?::::N::::J;::::?::::N::::J;::::?::::N::::J;::::?::::N::::J;::::?::::N::::J;::::?::::N::::J;::::?::::N::::J;::::?::::N::::J;::::?::::N::::J;::::?::::N::::J;::::?::::N::::J;::::?::::N::::J;::::?::::N::::J;::::?::::N::::J;::::?::::N::::J;::::?::::N::::J;::::?::::N::::J;::::?::::N::::J;::::?::::N::;;j@ERQ:::::<::::F:::::;::::?::::V::::J<::::G::::>;:::J@::::[::::>=:::JJ::::[;:::>B:::Jk::::;>:::>R:::J:<:::;F:::>:;::J:@:::;Z:::>:=::J:J:::;Z;::>:B::J:k:::;:>::>:R:>>zUgJ]B>J:::Z:;:::=>:::JJ:::J;;:::@>:::VJ::::<;:::C>:::_J:::Z<;:::D>:::gJ:::k<;:::F>:::kJ:::J=;:::G>:::oJ:::J=;:::H>:::sJ:::Z=;:::H>:::wJ:::k=;:::I>:::wJ::::>;:::J>::::K::::>;:::K>:::>K:::J>;:::K>:::>K:::J>;:::K>:::>K:::Z>;:::L>:::BK:::Z>;:::L>:::BK:::Z>;:::L>:::FK:::k>;:::M>:::FK:::k>;:::M>:::FK:::k>;:::N>:::JK::::?;:::N>:::JK::::?;:::N>:::JK:::J?;:::O>:::NK:::J?;:::O>:::NK:::J?;:::O>:::NK:::J?;:::O>:::NK:::J?;:::O>:::NK:::J?;:::P>:::RK:::Z?;:::P>:::RK:::Z?;:::P>:::RK:::Z?;:::P>:::RK:::Z?;:::P>:::RK:::Z?;:::P>:::VK:::k?;:::Q>:::VK:::k?;:::Q>:::VK:::k?;:::Q>:::VK:::k?;:::Q>:::VK:::k?;:::Q>:::VK::::@;:::R>:::ZK::::@;:::R>:::ZK::::@;:::R>:::ZK::::@;:::R>:::ZK::::@;:::R>:::ZK::::@;:::S>:::_K:::J@;:::S>:::_K:::J@;:::S>:::_K:::J@;:::S>:::_K:::J@;:::S>:::_K:::J@;:::S>:::_K:::J@;:::S>:::_K:::J@;:::S>:::_K:::J@;:::S>:::_K:::J@;:::S>:::_K:::J@;:::S>:::_K:::Z@;:::T>:::cK:::Z@;:::T>:::cK:::Z@;:::T>:::cK:::Z@;:::T>:::cK:::Z@;:::T>:::cK:::Z@;:::T>:::cK:::Z@;:::T>:::cK:::Z@;:::T>:::cK:::Z@;:::T>:::cK:::Z@;:::T>:::cK:::Z@;:::T>:::gK:::k@;:::U>:::gK:::k@;:::U>:::gK:::k@;:::U>:::gK:::k@;:::U>:::gK:::k@;:::U>:::gK:::k@;:::U>:::gK:::k@;:::U>:::gK:::k@;:::U>:::gK:::k@;:::U>:::gK:::k@;:::U>:::gK:::k@;:::V>:::kK::::A;:::V>:::kK::::A;:::V>:::kK::::A;:::V>:::kK::::A;:::V>:::kK::::A;:::V>:::kK::::A;:::V>:::kK::::A;:::V>:::kK::::A;:::V>:::kK::::A;:::V>:::kK::::A;:::V>:::oK:::Z:::::=::::V::;[BCEk:CkKbVNxqNoCQTiW@1".charCodeAt(Z++)]<<B,g=B=>N[k++]=J>>B;Z<36776;)J=F(0)|F(6)|F(12)|F(18),g(0),g(8),g(16);return N})(),C={},m={J:C,env:{__isb:()=>0,exit:B=>{g("EXIT","Exit called: "+B)},log:Math.log,sbrk:B=>{var k=N,c=k+B,L=c-Z.length;return c>F&&g("MEM","Out of memory"),L>0&&(J.grow(L+65535>>16),T()),N=c,k}}};WA.ZopfliDeflate=(J,c)=>{var L,N,F;return B.RUN((F=(N=(L=J).byteLength||L.length)&&B.malloc(N),Z.set(L,F),F),J.length,c||10),k},C.a=(B,J)=>{k=new Uint8Array(Z.subarray(B,B+J))};var G={module:new WebAssembly.Module(K)};G.instance=new WebAssembly.Instance(G.module,m);try{WA.wm=G.module,WA.asm=B=G.instance.exports;var e=WA.started;J=B.memory,T(),B.__wasm_call_ctors(),e&&e()}catch(B){"abort"!==B&&g("BOOT","WASM instiantate error: "+B+(B.stack?"\n"+B.stack:""))}})();
	return WA.ZopfliDeflate(buf, 100);
}
