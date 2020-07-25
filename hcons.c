/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2020 Vladimir Kondratyev <wulf@FreeBSD.org>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

/*
 * Consumer Controls usage page driver
 * https://www.usb.org/sites/default/files/documents/hut1_12v2.pdf
 */

#include <sys/param.h>
#include <sys/bitstring.h>
#include <sys/bus.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/sysctl.h>

#include <dev/evdev/input.h>
#include <dev/evdev/evdev.h>

#include "hid.h"
#include "hidbus.h"
#include "hmap.h"

#define	HID_DEBUG_VAR	hcons_debug
#include "hid_debug.h"

#ifdef HID_DEBUG
static int hcons_debug = 1;

static SYSCTL_NODE(_hw_hid, OID_AUTO, hcons, CTLFLAG_RW, 0,
		"Consumer Controls");
SYSCTL_INT(_hw_hid_hcons, OID_AUTO, debug, CTLFLAG_RWTUN,
		&hcons_debug, 0, "Debug level");
#endif

#define	HUC_CONSUMER_CONTROL	0x0001

#ifndef	KEY_FULL_SCREEN
#define KEY_FULL_SCREEN		0x174
#endif
#ifndef	KEY_ASPECT_RATIO
#define	KEY_ASPECT_RATIO	0x177
#endif
#ifndef	KEY_KBD_LAYOUT_NEXT
#define KEY_KBD_LAYOUT_NEXT	0x248
#endif

static hmap_cb_t	hcons_rel_volume_cb;

#define	HCONS_MAP_KEY(usage, code)	\
	{ HMAP_KEY(HUP_CONSUMER, usage, code) }
#define	HCONS_MAP_ABS(usage, code)	\
	{ HMAP_ABS(HUP_CONSUMER, usage, code) }
#define	HCONS_MAP_REL(usage, code)	\
	{ HMAP_REL(HUP_CONSUMER, usage, code) }
#define HCONS_MAP_REL_CB(usage, callback)	\
	{ HMAP_REL_CB(HUP_CONSUMER, usage, &callback) }

static const struct hmap_item hcons_map[] = {
	HCONS_MAP_KEY(0x030,	KEY_POWER),
	HCONS_MAP_KEY(0x031,	KEY_RESTART),
	HCONS_MAP_KEY(0x032,	KEY_SLEEP),
	HCONS_MAP_KEY(0x034,	KEY_SLEEP),
	HCONS_MAP_KEY(0x035,	KEY_KBDILLUMTOGGLE),
	HCONS_MAP_KEY(0x036,	BTN_MISC),
	HCONS_MAP_KEY(0x040,	KEY_MENU),	/* Menu */
	HCONS_MAP_KEY(0x041,	KEY_SELECT),	/* Menu Pick */
	HCONS_MAP_KEY(0x042,	KEY_UP),	/* Menu Up */
	HCONS_MAP_KEY(0x043,	KEY_DOWN),	/* Menu Down */
	HCONS_MAP_KEY(0x044,	KEY_LEFT),	/* Menu Left */
	HCONS_MAP_KEY(0x045,	KEY_RIGHT),	/* Menu Right */
	HCONS_MAP_KEY(0x046,	KEY_ESC),	/* Menu Escape */
	HCONS_MAP_KEY(0x047,	KEY_KPPLUS),	/* Menu Value Increase */
	HCONS_MAP_KEY(0x048,	KEY_KPMINUS),	/* Menu Value Decrease */
	HCONS_MAP_KEY(0x060,	KEY_INFO),	/* Data On Screen */
	HCONS_MAP_KEY(0x061,	KEY_SUBTITLE),	/* Closed Caption */
	HCONS_MAP_KEY(0x063,	KEY_VCR),	/* VCR/TV */
	HCONS_MAP_KEY(0x065,	KEY_CAMERA),	/* Snapshot */
	HCONS_MAP_KEY(0x069,	KEY_RED),
	HCONS_MAP_KEY(0x06a,	KEY_GREEN),
	HCONS_MAP_KEY(0x06b,	KEY_BLUE),
	HCONS_MAP_KEY(0x06c,	KEY_YELLOW),
	HCONS_MAP_KEY(0x06d,	KEY_ASPECT_RATIO),
	HCONS_MAP_KEY(0x06f,	KEY_BRIGHTNESSUP),
	HCONS_MAP_KEY(0x070,	KEY_BRIGHTNESSDOWN),
	HCONS_MAP_KEY(0x072,	KEY_BRIGHTNESS_TOGGLE),
	HCONS_MAP_KEY(0x073,	KEY_BRIGHTNESS_MIN),
	HCONS_MAP_KEY(0x074,	KEY_BRIGHTNESS_MAX),
	HCONS_MAP_KEY(0x075,	KEY_BRIGHTNESS_AUTO),
	HCONS_MAP_KEY(0x079,	KEY_KBDILLUMUP),
	HCONS_MAP_KEY(0x07a,	KEY_KBDILLUMDOWN),
	HCONS_MAP_KEY(0x07c,	KEY_KBDILLUMTOGGLE),
	HCONS_MAP_KEY(0x082,	KEY_VIDEO_NEXT),
	HCONS_MAP_KEY(0x083,	KEY_LAST),
	HCONS_MAP_KEY(0x084,	KEY_ENTER),
	HCONS_MAP_KEY(0x088,	KEY_PC),
	HCONS_MAP_KEY(0x089,	KEY_TV),
	HCONS_MAP_KEY(0x08a,	KEY_WWW),
	HCONS_MAP_KEY(0x08b,	KEY_DVD),
	HCONS_MAP_KEY(0x08c,	KEY_PHONE),
	HCONS_MAP_KEY(0x08d,	KEY_PROGRAM),
	HCONS_MAP_KEY(0x08e,	KEY_VIDEOPHONE),
	HCONS_MAP_KEY(0x08f,	KEY_GAMES),
	HCONS_MAP_KEY(0x090,	KEY_MEMO),
	HCONS_MAP_KEY(0x091,	KEY_CD),
	HCONS_MAP_KEY(0x092,	KEY_VCR),
	HCONS_MAP_KEY(0x093,	KEY_TUNER),
	HCONS_MAP_KEY(0x094,	KEY_EXIT),
	HCONS_MAP_KEY(0x095,	KEY_HELP),
	HCONS_MAP_KEY(0x096,	KEY_TAPE),
	HCONS_MAP_KEY(0x097,	KEY_TV2),
	HCONS_MAP_KEY(0x098,	KEY_SAT),
	HCONS_MAP_KEY(0x09a,	KEY_PVR),
	HCONS_MAP_KEY(0x09c,	KEY_CHANNELUP),
	HCONS_MAP_KEY(0x09d,	KEY_CHANNELDOWN),
	HCONS_MAP_KEY(0x0a0,	KEY_VCR2),
	HCONS_MAP_KEY(0x0b0,	KEY_PLAY),
	HCONS_MAP_KEY(0x0b1,	KEY_PAUSE),
	HCONS_MAP_KEY(0x0b2,	KEY_RECORD),
	HCONS_MAP_KEY(0x0b3,	KEY_FASTFORWARD),
	HCONS_MAP_KEY(0x0b4,	KEY_REWIND),
	HCONS_MAP_KEY(0x0b5,	KEY_NEXTSONG),
	HCONS_MAP_KEY(0x0b6,	KEY_PREVIOUSSONG),
	HCONS_MAP_KEY(0x0b7,	KEY_STOPCD),
	HCONS_MAP_KEY(0x0b8,	KEY_EJECTCD),
	HCONS_MAP_KEY(0x0bc,	KEY_MEDIA_REPEAT),
	HCONS_MAP_KEY(0x0b9,	KEY_SHUFFLE),
	HCONS_MAP_KEY(0x0bf,	KEY_SLOW),
	HCONS_MAP_KEY(0x0cd,	KEY_PLAYPAUSE),
	HCONS_MAP_KEY(0x0cf,	KEY_VOICECOMMAND),
	HCONS_MAP_ABS(0x0e0,	ABS_VOLUME),
	HCONS_MAP_REL_CB(0x0e0,	hcons_rel_volume_cb),
	HCONS_MAP_KEY(0x0e2,	KEY_MUTE),
	HCONS_MAP_KEY(0x0e5,	KEY_BASSBOOST),
	HCONS_MAP_KEY(0x0e9,	KEY_VOLUMEUP),
	HCONS_MAP_KEY(0x0ea,	KEY_VOLUMEDOWN),
	HCONS_MAP_KEY(0x0f5,	KEY_SLOW),
	HCONS_MAP_KEY(0x181,	KEY_BUTTONCONFIG),
	HCONS_MAP_KEY(0x182,	KEY_BOOKMARKS),
	HCONS_MAP_KEY(0x183,	KEY_CONFIG),
	HCONS_MAP_KEY(0x184,	KEY_WORDPROCESSOR),
	HCONS_MAP_KEY(0x185,	KEY_EDITOR),
	HCONS_MAP_KEY(0x186,	KEY_SPREADSHEET),
	HCONS_MAP_KEY(0x187,	KEY_GRAPHICSEDITOR),
	HCONS_MAP_KEY(0x188,	KEY_PRESENTATION),
	HCONS_MAP_KEY(0x189,	KEY_DATABASE),
	HCONS_MAP_KEY(0x18a,	KEY_MAIL),
	HCONS_MAP_KEY(0x18b,	KEY_NEWS),
	HCONS_MAP_KEY(0x18c,	KEY_VOICEMAIL),
	HCONS_MAP_KEY(0x18d,	KEY_ADDRESSBOOK),
	HCONS_MAP_KEY(0x18e,	KEY_CALENDAR),
	HCONS_MAP_KEY(0x18f,	KEY_TASKMANAGER),
	HCONS_MAP_KEY(0x190,	KEY_JOURNAL),
	HCONS_MAP_KEY(0x191,	KEY_FINANCE),
	HCONS_MAP_KEY(0x192,	KEY_CALC),
	HCONS_MAP_KEY(0x193,	KEY_PLAYER),
	HCONS_MAP_KEY(0x194,	KEY_FILE),
	HCONS_MAP_KEY(0x196,	KEY_WWW),
	HCONS_MAP_KEY(0x199,	KEY_CHAT),
	HCONS_MAP_KEY(0x19c,	KEY_LOGOFF),
	HCONS_MAP_KEY(0x19e,	KEY_COFFEE),
	HCONS_MAP_KEY(0x19f,	KEY_CONTROLPANEL),
	HCONS_MAP_KEY(0x1a2,	KEY_APPSELECT),
	HCONS_MAP_KEY(0x1a3,	KEY_NEXT),
	HCONS_MAP_KEY(0x1a4,	KEY_PREVIOUS),
	HCONS_MAP_KEY(0x1a6,	KEY_HELP),
	HCONS_MAP_KEY(0x1a7,	KEY_DOCUMENTS),
	HCONS_MAP_KEY(0x1ab,	KEY_SPELLCHECK),
	HCONS_MAP_KEY(0x1ae,	KEY_KEYBOARD),
	HCONS_MAP_KEY(0x1b1,	KEY_SCREENSAVER),
	HCONS_MAP_KEY(0x1b4,	KEY_FILE),
	HCONS_MAP_KEY(0x1b6,	KEY_IMAGES),
	HCONS_MAP_KEY(0x1b7,	KEY_AUDIO),
	HCONS_MAP_KEY(0x1b8,	KEY_VIDEO),
	HCONS_MAP_KEY(0x1bc,	KEY_MESSENGER),
	HCONS_MAP_KEY(0x1bd,	KEY_INFO),
	HCONS_MAP_KEY(0x1cb,	KEY_ASSISTANT),
	HCONS_MAP_KEY(0x201,	KEY_NEW),
	HCONS_MAP_KEY(0x202,	KEY_OPEN),
	HCONS_MAP_KEY(0x203,	KEY_CLOSE),
	HCONS_MAP_KEY(0x204,	KEY_EXIT),
	HCONS_MAP_KEY(0x207,	KEY_SAVE),
	HCONS_MAP_KEY(0x208,	KEY_PRINT),
	HCONS_MAP_KEY(0x209,	KEY_PROPS),
	HCONS_MAP_KEY(0x21a,	KEY_UNDO),
	HCONS_MAP_KEY(0x21b,	KEY_COPY),
	HCONS_MAP_KEY(0x21c,	KEY_CUT),
	HCONS_MAP_KEY(0x21d,	KEY_PASTE),
	HCONS_MAP_KEY(0x21f,	KEY_FIND),
	HCONS_MAP_KEY(0x221,	KEY_SEARCH),
	HCONS_MAP_KEY(0x222,	KEY_GOTO),
	HCONS_MAP_KEY(0x223,	KEY_HOMEPAGE),
	HCONS_MAP_KEY(0x224,	KEY_BACK),
	HCONS_MAP_KEY(0x225,	KEY_FORWARD),
	HCONS_MAP_KEY(0x226,	KEY_STOP),
	HCONS_MAP_KEY(0x227,	KEY_REFRESH),
	HCONS_MAP_KEY(0x22a,	KEY_BOOKMARKS),
	HCONS_MAP_KEY(0x22d,	KEY_ZOOMIN),
	HCONS_MAP_KEY(0x22e,	KEY_ZOOMOUT),
	HCONS_MAP_KEY(0x22f,	KEY_ZOOMRESET),
	HCONS_MAP_KEY(0x232,	KEY_FULL_SCREEN),
	HCONS_MAP_KEY(0x233,	KEY_SCROLLUP),
	HCONS_MAP_KEY(0x234,	KEY_SCROLLDOWN),
	HCONS_MAP_REL(0x238,	REL_HWHEEL),	 /* AC Pan */
	HCONS_MAP_KEY(0x23d,	KEY_EDIT),
	HCONS_MAP_KEY(0x25f,	KEY_CANCEL),
	HCONS_MAP_KEY(0x269,	KEY_INSERT),
	HCONS_MAP_KEY(0x26a,	KEY_DELETE),
	HCONS_MAP_KEY(0x279,	KEY_REDO),
	HCONS_MAP_KEY(0x289,	KEY_REPLY),
	HCONS_MAP_KEY(0x28b,	KEY_FORWARDMAIL),
	HCONS_MAP_KEY(0x28c,	KEY_SEND),
	HCONS_MAP_KEY(0x29d,	KEY_KBD_LAYOUT_NEXT),
	HCONS_MAP_KEY(0x2c7,	KEY_KBDINPUTASSIST_PREV),
	HCONS_MAP_KEY(0x2c8,	KEY_KBDINPUTASSIST_NEXT),
	HCONS_MAP_KEY(0x2c9,	KEY_KBDINPUTASSIST_PREVGROUP),
	HCONS_MAP_KEY(0x2ca,	KEY_KBDINPUTASSIST_NEXTGROUP),
	HCONS_MAP_KEY(0x2cb,	KEY_KBDINPUTASSIST_ACCEPT),
	HCONS_MAP_KEY(0x2cc,	KEY_KBDINPUTASSIST_CANCEL),
	HCONS_MAP_KEY(0x29f,	KEY_SCALE),
};

static const struct hid_device_id hcons_devs[] = {
	{ HID_TLC(HUP_CONSUMER, HUC_CONSUMER_CONTROL) },
};

/*
 * Emulate relative Consumer volume usage with pressing
 * VOLUMEUP and VOLUMEDOWN keys appropriate number of times
 */
static int
hcons_rel_volume_cb(HMAP_CB_ARGS)
{
	struct evdev_dev *evdev = HMAP_CB_GET_EVDEV();
	int32_t data;
	int32_t code;
	int nrepeats;

	switch (HMAP_CB_GET_STATE()) {
	case HMAP_CB_IS_ATTACHING:
		evdev_support_event(evdev, EV_KEY);
		evdev_support_key(evdev, KEY_VOLUMEUP);
		evdev_support_key(evdev, KEY_VOLUMEDOWN);
		break;
	case HMAP_CB_IS_RUNNING:
		data = ctx;
		/* Nothing to report. */
		if (data == 0)
			return (ENOMSG);
		code = data > 0 ? KEY_VOLUMEUP : KEY_VOLUMEDOWN;
		for (nrepeats = abs(data); nrepeats > 0; nrepeats--) {
			evdev_push_key(evdev, code, 1);
			evdev_push_key(evdev, code, 0);
		}
	}

	return (0);
}

static int
hcons_probe(device_t dev)
{
	struct hmap *hm = device_get_softc(dev);
	int error;

	error = hidbus_lookup_driver_info(dev, hcons_devs, sizeof(hcons_devs));
	if (error != 0)
		return (error);

	hmap_set_dev(hm, dev);
	hmap_set_debug_var(hm, &HID_DEBUG_VAR);

	/* Check if report descriptor belongs to a Consumer controls page */
	error = hmap_add_map(hm, hcons_map, nitems(hcons_map), NULL);
	if (error != 0)
		return (error);

	hidbus_set_desc(dev, "Consumer Control");

	return (BUS_PROBE_DEFAULT);
}

static int
hcons_attach(device_t dev)
{
	return (hmap_attach(device_get_softc(dev)));
}

static int
hcons_detach(device_t dev)
{
	return (hmap_detach(device_get_softc(dev)));
}

static devclass_t hcons_devclass;
static device_method_t hcons_methods[] = {
	DEVMETHOD(device_probe,		hcons_probe),
	DEVMETHOD(device_attach,	hcons_attach),
	DEVMETHOD(device_detach,	hcons_detach),

	DEVMETHOD_END
};

DEFINE_CLASS_0(hcons, hcons_driver, hcons_methods, sizeof(struct hmap));
DRIVER_MODULE(hcons, hidbus, hcons_driver, hcons_devclass, NULL, 0);
MODULE_DEPEND(hcons, hid, 1, 1, 1);
MODULE_DEPEND(hcons, hmap, 1, 1, 1);
MODULE_DEPEND(hcons, evdev, 1, 1, 1);
MODULE_VERSION(hcons, 1);
