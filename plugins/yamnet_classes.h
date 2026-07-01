#ifndef YAMNET_CLASSES_H
#define YAMNET_CLASSES_H

#include <stdint.h>
#include <string.h>

#define NUM_BUCKETS	10
#define BUCKET_SPEECH	0
#define BUCKET_ALERT	1
#define BUCKET_LAUGH	2
#define BUCKET_CROWD	3
#define BUCKET_HVAC	4
#define BUCKET_NOISE	5
#define BUCKET_DOOR	6
#define BUCKET_MUSIC	7
#define BUCKET_TYPING	8
#define BUCKET_APPLAUSE	9

static inline void yamnet_init_class_buckets(uint8_t cb[521])
{
	memset(cb, 0xFF, 521);

	/* BUCKET_SPEECH (0) */
	cb[0] = BUCKET_SPEECH; /* Speech */
	cb[1] = BUCKET_SPEECH; /* Child speech, kid speaking */
	cb[2] = BUCKET_SPEECH; /* Conversation */
	cb[3] = BUCKET_SPEECH; /* Narration, monologue */
	cb[4] = BUCKET_SPEECH; /* Babbling */
	cb[12] = BUCKET_SPEECH; /* Whispering */

	/* BUCKET_ALERT (1) */
	cb[304] = BUCKET_ALERT; /* Car alarm */
	cb[317] = BUCKET_ALERT; /* Police car (siren) */
	cb[318] = BUCKET_ALERT; /* Ambulance (siren) */
	cb[319] = BUCKET_ALERT; /* Fire engine siren */
	cb[349] = BUCKET_ALERT; /* Doorbell */
	cb[350] = BUCKET_ALERT; /* Ding-dong */
	cb[353] = BUCKET_ALERT; /* Knock */
	cb[354] = BUCKET_ALERT; /* Tap */
	cb[382] = BUCKET_ALERT; /* Alarm */
	cb[384] = BUCKET_ALERT; /* Telephone bell ringing */
	cb[385] = BUCKET_ALERT; /* Ringtone */
	cb[389] = BUCKET_ALERT; /* Alarm clock */
	cb[390] = BUCKET_ALERT; /* Siren */
	cb[391] = BUCKET_ALERT; /* Civil defense siren */
	cb[392] = BUCKET_ALERT; /* Buzzer */
	cb[393] = BUCKET_ALERT; /* Smoke detector, smoke alarm */
	cb[394] = BUCKET_ALERT; /* Fire alarm */
	cb[475] = BUCKET_ALERT; /* Beep, bleep */

	/* BUCKET_LAUGH (2) */
	cb[13] = BUCKET_LAUGH; /* Laughter */
	cb[14] = BUCKET_LAUGH; /* Baby laughter */
	cb[15] = BUCKET_LAUGH; /* Giggle */
	cb[16] = BUCKET_LAUGH; /* Snicker */
	cb[17] = BUCKET_LAUGH; /* Belly laugh */
	cb[18] = BUCKET_LAUGH; /* Chuckle, chortle */

	/* BUCKET_CROWD (3) */
	cb[63] = BUCKET_CROWD; /* Chatter */
	cb[64] = BUCKET_CROWD; /* Crowd */
	cb[65] = BUCKET_CROWD; /* Hubbub, speech noise, speech babble */

	/* BUCKET_HVAC (4) */
	cb[277] = BUCKET_HVAC; /* Wind */
	cb[279] = BUCKET_HVAC; /* Wind noise (microphone) */
	cb[406] = BUCKET_HVAC; /* Mechanical fan */
	cb[407] = BUCKET_HVAC; /* Air conditioning */
	cb[490] = BUCKET_HVAC; /* Hum */
	cb[510] = BUCKET_HVAC; /* Environment noise */
	cb[514] = BUCKET_HVAC; /* Sidetone */
	cb[515] = BUCKET_HVAC; /* Pink noise */

	/* BUCKET_NOISE (5) */
	cb[283] = BUCKET_NOISE; /* Rain */
	cb[321] = BUCKET_NOISE; /* Traffic noise, roadway noise */
	cb[337] = BUCKET_NOISE; /* Engine */

	/* BUCKET_DOOR (6) */
	cb[348] = BUCKET_DOOR; /* Door */
	cb[351] = BUCKET_DOOR; /* Sliding door */
	cb[352] = BUCKET_DOOR; /* Slam */

	/* BUCKET_MUSIC (7) */
	cb[132] = BUCKET_MUSIC; /* Music */
	cb[262] = BUCKET_MUSIC; /* Background music */
	cb[263] = BUCKET_MUSIC; /* Theme music */

	/* BUCKET_TYPING (8) */
	cb[378] = BUCKET_TYPING; /* Typing */
	cb[379] = BUCKET_TYPING; /* Typewriter */
	cb[380] = BUCKET_TYPING; /* Computer keyboard */

	/* BUCKET_APPLAUSE (9) */
	cb[56] = BUCKET_APPLAUSE; /* Hands */
	cb[57] = BUCKET_APPLAUSE; /* Finger snapping */
	cb[58] = BUCKET_APPLAUSE; /* Clapping */
	cb[61] = BUCKET_APPLAUSE; /* Cheering */
	cb[62] = BUCKET_APPLAUSE; /* Applause */
}

#endif /* YAMNET_CLASSES_H */
