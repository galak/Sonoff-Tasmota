/*
  xdrv_99_haiku_fan.ino - haiku fan support for Sonoff-Tasmota

  Copyright (C) 2018  Kumar Gala

  This program is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#ifdef USE_HAIKU_FAN

WiFiClient HaikuClient;

/*********************************************************************************************\
 * Interface
\*********************************************************************************************/

#define XDRV_98	98

#define D_CMND_FANIPADDR "FanIPAddress"
#define D_CMND_HAIKU_TYPE "HaikuType"

const char kfanCommands[] PROGMEM = "|"  // No prefix
  D_CMND_FANSPEED "|" D_CMND_FANIPADDR "|" D_CMND_HAIKU_TYPE;

void (* const fanCommand[])(void) PROGMEM = {
  &CmndFanspeed, &CmndFanIpAddress, &CmndHaikuType};

#define HAIKU_PORT 31415

char cmd_buf[200];     // buffer to hold incoming UDP packet

struct HAIKU_CMD {
	enum haikuType type;
	enum haikuCmdType cmd;
	uint8_t val;
} haiku_cmd;

struct HAIKU {
	IPAddress ip;
	enum haikuType type;
	char name[40];
	uint8_t level;
	power_t power;
} haiku;

bool haiku_initialized = false;

#if 0
const char * const haiku_type_str[] PROGMEM = { "fan", "light" };
const char * const haiku_cmd_str[] PROGMEM = { "on", "off", "level" };

void haiku_cmd_log(const char *s)
{
	AddLog_P2(LOG_LEVEL_INFO,
		 PSTR("HAIKU CMD[%s] [%s] type: [%s] cmd: [%s] val: [%d]"),
		 s,
		 haiku.name,
		 haiku_type_str[haiku_cmd.type],
		 haiku_cmd_str[haiku_cmd.cmd],
		 haiku_cmd.val);
}
#else
void haiku_cmd_log(const char *s)
{
	return;
}
#endif

void CmndFanIpAddress(void)
{
  if ((XdrvMailbox.index > 0) && (XdrvMailbox.index <= 4)) {
    uint32_t address;
    if (ParseIp(&address, XdrvMailbox.data)) {
      Settings.haiku_ip_address = address;
      AddLog_P2(LOG_LEVEL_INFO, PSTR("SET FAN IP %x"), Settings.haiku_ip_address);
      haiku.ip = Settings.haiku_ip_address;
    }
  }

  if (!haiku.ip.isSet()) {
	  AddLog_P2(LOG_LEVEL_INFO, PSTR("FAN IP NOT SET"));
  }

  AddLog_P2(LOG_LEVEL_INFO, PSTR("FAN IP %u.%u.%u.%u"),
	    haiku.ip[0], haiku.ip[1], haiku.ip[2], haiku.ip[3]);

  AddLog_P2(LOG_LEVEL_INFO, PSTR("FAN IP S:%s"), haiku.ip.toString().c_str());

  Response_P(PSTR("{\"" D_CMND_FANIPADDR "\":{\"IP\":%x}}"),
	     Settings.haiku_ip_address);
}

void CmndHaikuType(void)
{
	if (XdrvMailbox.data_len) {
		uint8_t haiku_type;

		if (strcasecmp(XdrvMailbox.data, "FAN") == 0) {
			haiku_type = (uint8_t)HAIKU_FAN;
		}
		if (strcasecmp(XdrvMailbox.data, "LIGHT") == 0) {
			haiku_type = (uint8_t)HAIKU_LIGHT;
		}
		if (haiku_type != Settings.haiku_type) {
			Settings.haiku_type = haiku_type;
			haiku.type = (enum haikuType)haiku_type;
			/* TODO: should re-query state at this point */
		}
	}

	if (Settings.haiku_type == HAIKU_FAN) {
		ResponseCmndChar_P(PSTR("Fan"));
	} else {
		ResponseCmndChar_P(PSTR("Light"));
	}
}

/*
Fan: [0:7]
<Office Fan;FAN;SPD;GET;ACTUAL>
<Office Fan;FAN;SPD;SET;7>
*/

/* Light level [0:16]
<Office Fan;LIGHT;LEVEL;GET;ACTUAL>
*/

/* Light level [0:16]
<Office Fan;LIGHT;LEVEL;SET;16>
*/


bool haiku_connected = false;

uint8_t HaikuGetCmd(enum haikuType t)
{
	uint8_t val = -1;
	String cmd = FPSTR("<");

	cmd += haiku.name;

	if (t == HAIKU_FAN) {
		cmd += FPSTR(";FAN;SPD;GET;ACTUAL>");
	} else {
		cmd += FPSTR(";LIGHT;LEVEL;GET;ACTUAL>");
	}

//	AddLog_P2(LOG_LEVEL_DEBUG, PSTR("haiku: getcmd: cmd str: %s"), cmd.c_str());

	HaikuClient.println(cmd);

	int count = 0;
	while ((HaikuClient.available() == 0) && (count < 100)) {
		count++;
		delay(100);
	}

	val = GetCommand();

	return val;
}

void HaikuSetCmd(enum haikuType t, enum haikuCmdType cmd_type, uint8_t val)
{
	String cmd = FPSTR("<");

	cmd += haiku.name;

	if (t == HAIKU_FAN) {
		cmd += FPSTR(";FAN;");
	} else {
		cmd += FPSTR(";LIGHT;");
	}

	switch (cmd_type) {
		case HAIKU_CMD_LEVEL:
			if (t == HAIKU_FAN) {
				cmd += FPSTR("SPD;SET;");
			} else {
				cmd += FPSTR("LEVEL;SET;");
			}
			cmd += val;
			break;
		case HAIKU_CMD_ON:
			cmd += FPSTR("PWR;ON");
			break;
		case HAIKU_CMD_OFF:
			cmd += FPSTR("PWR;OFF");
			break;
	}

	cmd += FPSTR(">");

	AddLog_P2(LOG_LEVEL_DEBUG, PSTR("haiku: setcmd: cmd str: %s"), cmd.c_str());

	HaikuClient.println(cmd);

	int count = 0;
	while ((HaikuClient.available() == 0) && (count < 100)) {
		count++;
		delay(100);
	}

	val = GetCommand();
}

void haiku_init(void)
{
	if (!haiku.ip.isSet())
		return;

	if (HaikuClient.connect(haiku.ip, HAIKU_PORT)) {
		AddLog_P2(LOG_LEVEL_DEBUG, PSTR("connect haiku conn"));

		String cmd = FPSTR("<ALL;DEVICE;ID;GET>");

		HaikuClient.println(cmd);

		int count = 0;
		while ((HaikuClient.available() == 0) && (count < 100)) {
			count++;
			delay(100);
		}

		AddLog_P2(LOG_LEVEL_DEBUG, PSTR("haiku: init: count %d len %d/%d"), count, HaikuClient.available());

		GetCommand();

		haiku_cmd_log("init");

		/* We connected and fan name was set */
		if (haiku.name[0] != '\0') {
			haiku.level = HaikuGetCmd(haiku.type);

			AddLog_P2(LOG_LEVEL_INFO, PSTR("haiku: init: level %d"), haiku.level);
			haiku_connected = true;
			if (haiku.level)
				haiku.power = 1;
		}
	}
}

uint8_t process_cmd(char * cmd)
{
	uint8_t val;
	char *str;

	if (cmd[0] != '(') {
		return 0;
	}

	/* name */
	str = strtok(cmd_buf+1, ";");
//	AddLog_P2(LOG_LEVEL_DEBUG, PSTR("PROCESS CMD - name: %s"), str);

	if (haiku.name[0] == '\0') {
		strcpy(haiku.name, str);
//		AddLog_P2(LOG_LEVEL_DEBUG, PSTR("haiku: processcmd: set name: %s"), haiku.name);
	}

	/* type - FAN or LIGHT */
	str = strtok(NULL, ";");
//	AddLog_P2(LOG_LEVEL_DEBUG, PSTR("PROCESS CMD - type: %s"), str);

	if (strcmp(str, "FAN") == 0) {
		haiku_cmd.type = HAIKU_FAN;
	} else {
		haiku_cmd.type = HAIKU_LIGHT;
	}

	/* cmd - PWR, LEVEL (for light) or SPD (for fan) */
	str = strtok(NULL, ";");
//	AddLog_P2(LOG_LEVEL_DEBUG, PSTR("PROCESS CMD - cmd: %s"), str);

	if (strcmp(str, "PWR") == 0) {
		/* val - ON/OFF */
		str = strtok(NULL, ";");
		if (strcmp(str, "ON") == 0) {
			val = 1;
			haiku_cmd.cmd = HAIKU_CMD_ON;
		} else {
			val = 0;
			haiku_cmd.cmd = HAIKU_CMD_OFF;
		}
//		AddLog_P2(LOG_LEVEL_DEBUG, PSTR("PROCESS CMD - val: %d"), val);
	} else {
		/* ACTUAL */
		str = strtok(NULL, ";");
//		AddLog_P2(LOG_LEVEL_DEBUG, PSTR("actual %s"), str);

		str = strtok(NULL, ";");

		val = strtol(str, nullptr, 10);

		haiku_cmd.cmd = HAIKU_CMD_LEVEL;
		haiku_cmd.val = val;

//		AddLog_P2(LOG_LEVEL_DEBUG, PSTR("PROCESS CMD - val: %d"), val);
	}

	return val;
}


int last_char = 0;

int GetCommand(void)
{
	int val = -1;
	int i, len;

	len = HaikuClient.available();
	if (len) {
//		AddLog_P2(LOG_LEVEL_DEBUG, PSTR("HC pkt len: %d"), len);
		for (i = 0; i < len; i++) {
			char c = HaikuClient.read();
			cmd_buf[last_char] = c;
			if (c == ')') {
				cmd_buf[last_char] = '\0';
				last_char = 0;
//				AddLog_P2(LOG_LEVEL_DEBUG, PSTR("HC cmd: [%s]"), cmd_buf);
				val = process_cmd(cmd_buf);
				break;
			}
			last_char++;
		}

//		AddLog_P2(LOG_LEVEL_DEBUG, PSTR("HC end data %d"), HaikuClient.available());
	}

	return val;
}


#define MAX_FAN_SPEED 8
#define MAX_LIGHT_LEVEL 15

/* CMD: <Office Fan;FAN;SPD;GET;ACTUAL> */
/* RESPONSE: (Office Fan;FAN;SPD;ACTUAL;4) */

/* <Office Fan;FAN;SPD;SET;7> */
/* response: (Office Fan;FAN;PWR;ON)(Office Fan;FAN;SPD;ACTUAL;4)(Office Fan;FAN;SPD;ACTUAL;4) */

void CmndFanspeed(void)
{
  if (XdrvMailbox.data_len > 0) {
    if ('-' == XdrvMailbox.data[0]) {
      XdrvMailbox.payload = (int16_t)HaikuGetCmd(HAIKU_FAN) - 1;
      if (XdrvMailbox.payload < 0) { XdrvMailbox.payload = MAX_FAN_SPEED -1; }
    }
    else if ('+' == XdrvMailbox.data[0]) {
      XdrvMailbox.payload = HaikuGetCmd(HAIKU_FAN) + 1;
      if (XdrvMailbox.payload > MAX_FAN_SPEED -1) { XdrvMailbox.payload = 0; }
    }
  }
  if ((XdrvMailbox.payload >= 0) && (XdrvMailbox.payload < MAX_FAN_SPEED)) {
    HaikuSetCmd(HAIKU_FAN, HAIKU_CMD_LEVEL, XdrvMailbox.payload);
  }
  ResponseCmndNumber(HaikuGetCmd(HAIKU_FAN));
}

void HaikuAnyKey(void)
{
	uint32_t key = (XdrvMailbox.payload >> 16) & 0xFF;   // 0 = Button, 1 = Switch
	uint32_t device = XdrvMailbox.payload & 0xFF;        // Device number or 1 if more Buttons than Devices
	uint32_t state = (XdrvMailbox.payload >> 8) & 0xFF;  // 0 = Off, 1 = On, 2 = Toggle, 3 = Hold, 10,11,12,13 and 14 for Button Multipress

	AddLog_P2(LOG_LEVEL_INFO, PSTR("Haiku KEY: %d DEV: %d STATE: %d"), key, device, state);

	/* Lower */
	/* Button 2 - state 2 (toggle) */
	if ((device == 2) && (state == 2)) {
		if (haiku.level != 0) {
			haiku.level--;
			AddLog_P2(LOG_LEVEL_INFO, PSTR("Haiku anykey--: %d"), haiku.level);
			HaikuSetCmd(HAIKU_FAN, HAIKU_CMD_LEVEL, haiku.level);
		}
	}

	/* Up button */
	if ((device == 3) && (state == 2)) {
		if (haiku.level < MAX_FAN_SPEED - 1) {
			haiku.level++;
			AddLog_P2(LOG_LEVEL_INFO, PSTR("Haiku anykey++: %d"), haiku.level);
			HaikuSetCmd(HAIKU_FAN, HAIKU_CMD_LEVEL, haiku.level);
		}
	}
}

bool Xdrv98(uint8_t function)
{
	bool result = false;
	int len, i, ret;

	switch (function) {
		case FUNC_INIT:
			haiku.ip = Settings.haiku_ip_address;
			haiku.type = (enum haikuType)Settings.haiku_type;
			break;
		case FUNC_LOOP:
			if (haiku_connected) {
				ret = GetCommand();

				if ((ret >= 0) && (haiku_cmd.type == haiku.type)) {
					haiku_cmd_log("LOOP");
					switch (haiku_cmd.cmd) {
						case HAIKU_CMD_LEVEL:
							break;
						case HAIKU_CMD_OFF:
							if (haiku.power == POWER_ON)
								haiku.power = POWER_OFF;
								ExecuteCommandPower(1, POWER_OFF, SRC_REMOTE);
							break;
						case HAIKU_CMD_ON:
							if (haiku.power == POWER_OFF)
								haiku.power = POWER_ON;
								ExecuteCommandPower(1, POWER_ON, SRC_REMOTE);
							break;
					}
				}
			}
			break;
		case FUNC_COMMAND:
			result = DecodeCommand(kfanCommands, fanCommand);
			break;
		case FUNC_EVERY_SECOND:
			if (!haiku_connected) {
				haiku_init();
			}
			break;
		case FUNC_SET_POWER:
			AddLog_P2(LOG_LEVEL_INFO, PSTR("Haiku SET POWER: %d hk.power %d"), XdrvMailbox.index, haiku.power);
			if (XdrvMailbox.index != haiku.power) {
				/* ON */
				if (XdrvMailbox.index) {
					HaikuSetCmd(haiku.type, HAIKU_CMD_ON, 0);
				} else {
					HaikuSetCmd(haiku.type, HAIKU_CMD_OFF, 0);
				}
				haiku_cmd_log("SET_PWR");
				haiku.power = XdrvMailbox.index;
				AddLog_P2(LOG_LEVEL_INFO, PSTR(""));
			}
			result = true;
			break;
		case FUNC_ANY_KEY:
			HaikuAnyKey();
			result = true;
			break;
	}
	return result;
}

#endif  // USE_HAIKU_FAN
