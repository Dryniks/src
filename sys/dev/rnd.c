/*	$OpenBSD: rnd.c,v 1.43 2000/08/21 01:52:18 jason Exp $	*/

/*
 * random.c -- A strong random number generator
 *
 * Copyright (c) 1996, 1997, 2000 Michael Shalayeff.
 *
 * Version 1.89, last modified 19-Sep-99
 *
 * Copyright Theodore Ts'o, 1994, 1995, 1996, 1997, 1998, 1999.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, and the entire permission notice in its entirety,
 *    including the disclaimer of warranties.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The name of the author may not be used to endorse or promote
 *    products derived from this software without specific prior
 *    written permission.
 *
 * ALTERNATIVELY, this product may be distributed under the terms of
 * the GNU Public License, in which case the provisions of the GPL are
 * required INSTEAD OF the above restrictions.  (This clause is
 * necessary due to a potential bad interaction between the GPL and
 * the restrictions contained in a BSD-style copyright.)
 *
 * THIS SOFTWARE IS PROVIDED ``AS IS'' AND ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED
 * OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * (now, with legal B.S. out of the way.....)
 *
 * This routine gathers environmental noise from device drivers, etc.,
 * and returns good random numbers, suitable for cryptographic use.
 * Besides the obvious cryptographic uses, these numbers are also good
 * for seeding TCP sequence numbers, and other places where it is
 * desirable to have numbers which are not only random, but hard to
 * predict by an attacker.
 *
 * Theory of operation
 * ===================
 *
 * Computers are very predictable devices.  Hence it is extremely hard
 * to produce truly random numbers on a computer --- as opposed to
 * pseudo-random numbers, which can easily generated by using a
 * algorithm.  Unfortunately, it is very easy for attackers to guess
 * the sequence of pseudo-random number generators, and for some
 * applications this is not acceptable.  So instead, we must try to
 * gather "environmental noise" from the computer's environment, which
 * must be hard for outside attackers to observe, and use that to
 * generate random numbers.  In a Unix environment, this is best done
 * from inside the kernel.
 *
 * Sources of randomness from the environment include inter-keyboard
 * timings, inter-interrupt timings from some interrupts, and other
 * events which are both (a) non-deterministic and (b) hard for an
 * outside observer to measure.  Randomness from these sources are
 * added to an "entropy pool", which is mixed using a CRC-like function.
 * This is not cryptographically strong, but it is adequate assuming
 * the randomness is not chosen maliciously, and it is fast enough that
 * the overhead of doing it on every interrupt is very reasonable.
 * As random bytes are mixed into the entropy pool, the routines keep
 * an *estimate* of how many bits of randomness have been stored into
 * the random number generator's internal state.
 *
 * When random bytes are desired, they are obtained by taking the MD5
 * hash of the contents of the "entropy pool".  The MD5 hash avoids
 * exposing the internal state of the entropy pool.  It is believed to
 * be computationally infeasible to derive any useful information
 * about the input of MD5 from its output.  Even if it is possible to
 * analyze MD5 in some clever way, as long as the amount of data
 * returned from the generator is less than the inherent entropy in
 * the pool, the output data is totally unpredictable.  For this
 * reason, the routine decreases its internal estimate of how many
 * bits of "true randomness" are contained in the entropy pool as it
 * outputs random numbers.
 *
 * If this estimate goes to zero, the routine can still generate
 * random numbers; however, an attacker may (at least in theory) be
 * able to infer the future output of the generator from prior
 * outputs.  This requires successful cryptanalysis of MD5, which is
 * not believed to be feasible, but there is a remote possibility.
 * Nonetheless, these numbers should be useful for the vast majority
 * of purposes.
 *
 * Exported interfaces ---- output
 * ===============================
 *
 * There are three exported interfaces; the first is one designed to
 * be used from within the kernel:
 *
 *	void get_random_bytes(void *buf, int nbytes);
 *
 * This interface will return the requested number of random bytes,
 * and place it in the requested buffer.
 *
 * The two other interfaces are two character devices /dev/random and
 * /dev/urandom.  /dev/random is suitable for use when very high
 * quality randomness is desired (for example, for key generation or
 * one-time pads), as it will only return a maximum of the number of
 * bits of randomness (as estimated by the random number generator)
 * contained in the entropy pool.
 *
 * The /dev/urandom device does not have this limit, and will return
 * as many bytes as are requested.  As more and more random bytes are
 * requested without giving time for the entropy pool to recharge,
 * this will result in random numbers that are merely cryptographically
 * strong.  For many applications, however, this is acceptable.
 *
 * Exported interfaces ---- input
 * ==============================
 *
 * The current exported interfaces for gathering environmental noise
 * from the devices are:
 *
 *	void add_true_randomness(int data);
 *	void add_timer_randomness(int data);
 *	void add_mouse_randomness(int mouse_data);
 *	void add_net_randomness(int isr);
 *	void add_tty_randomness(int c);
 *	void add_disk_randomness(int n);
 *	void add_audio_randomness(int n);
 *
 * add_true_randomness() uses true random number generators present
 * on some cryptographic and system chipsets. entropy accounting
 * is not quitable, no timing is done, supplied 32 bits of pure entropy
 * are hashed into the pool plain and blindly, increasing the counter.
 *
 * add_timer_randomness() uses the random driver itselves timing,
 * measuring extract_entropy() and rndioctl() execution times.
 *
 * add_mouse_randomness() uses the mouse interrupt timing, as well as
 * the reported position of the mouse from the hardware.
 *
 * add_net_randomness() times the finishing time of net input.
 *
 * add_tty_randomness() uses the inter-keypress timing, as well as the
 * character as random inputs into the "entropy pool".
 *
 * add_disk_randomness() times the finishing time of disk requests as well
 * as feeding both xfer size & time into the entropy pool.
 *
 * add_audio_randomness() times the finishing of audio codec dma
 * requests for both recording and playback, apparently supplies quite
 * a lot of entropy, i'd blame on low resolution audio clock generators.
 *
 * All of these routines (except for add_true_randomness() of course)
 * try to estimate how many bits of randomness a particular randomness
 * source.  They do this by keeping track of the first and second order
 * deltas of the event timings.
 *
 * Ensuring unpredictability at system startup
 * ============================================
 *
 * When any operating system starts up, it will go through a sequence
 * of actions that are fairly predictable by an adversary, especially
 * if the start-up does not involve interaction with a human operator.
 * This reduces the actual number of bits of unpredictability in the
 * entropy pool below the value in entropy_count.  In order to
 * counteract this effect, it helps to carry information in the
 * entropy pool across shut-downs and start-ups.  To do this, put the
 * following lines an appropriate script which is run during the boot
 * sequence:
 *
 *	echo "Initializing random number generator..."
 *	# Carry a random seed from start-up to start-up
 *	# Load and then save 512 bytes, which is the size of the entropy pool
 *	if [ -f /etc/random-seed ]; then
 *		cat /etc/random-seed >/dev/urandom
 *	fi
 *	dd if=/dev/urandom of=/etc/random-seed count=1
 *
 * and the following lines in an appropriate script which is run as
 * the system is shutdown:
 *
 *	# Carry a random seed from shut-down to start-up
 *	# Save 512 bytes, which is the size of the entropy pool
 *	echo "Saving random seed..."
 *	dd if=/dev/urandom of=/etc/random-seed count=1
 *
 * For example, on many Linux systems, the appropriate scripts are
 * usually /etc/rc.d/rc.local and /etc/rc.d/rc.0, respectively.
 *
 * Effectively, these commands cause the contents of the entropy pool
 * to be saved at shut-down time and reloaded into the entropy pool at
 * start-up.  (The 'dd' in the addition to the bootup script is to
 * make sure that /etc/random-seed is different for every start-up,
 * even if the system crashes without executing rc.0.)  Even with
 * complete knowledge of the start-up activities, predicting the state
 * of the entropy pool requires knowledge of the previous history of
 * the system.
 *
 * Configuring the /dev/random driver under Linux
 * ==============================================
 *
 * The /dev/random driver under Linux uses minor numbers 8 and 9 of
 * the /dev/mem major number (#1).  So if your system does not have
 * /dev/random and /dev/urandom created already, they can be created
 * by using the commands:
 *
 *	mknod /dev/random c 1 8
 *	mknod /dev/urandom c 1 9
 *
 * Acknowledgements:
 * =================
 *
 * Ideas for constructing this random number generator were derived
 * from Pretty Good Privacy's random number generator, and from private
 * discussions with Phil Karn.  Colin Plumb provided a faster random
 * number generator, which speed up the mixing function of the entropy
 * pool, taken from PGPfone.  Dale Worley has also contributed many
 * useful ideas and suggestions to improve this driver.
 *
 * Any flaws in the design are solely my responsibility, and should
 * not be attributed to the Phil, Colin, or any of authors of PGP.
 *
 * The code for SHA transform was taken from Peter Gutmann's
 * implementation, which has been placed in the public domain.
 * The code for MD5 transform was taken from Colin Plumb's
 * implementation, which has been placed in the public domain.
 * The MD5 cryptographic checksum was devised by Ronald Rivest, and is
 * documented in RFC 1321, "The MD5 Message Digest Algorithm".
 *
 * Further background information on this topic may be obtained from
 * RFC 1750, "Randomness Recommendations for Security", by Donald
 * Eastlake, Steve Crocker, and Jeff Schiller.
 */

#undef RNDEBUG

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/conf.h>
#include <sys/disk.h>
#include <sys/ioctl.h>
#include <sys/malloc.h>
#include <sys/fcntl.h>
#include <sys/vnode.h>
#include <sys/md5k.h>
#include <sys/sysctl.h>
#include <sys/timeout.h>

#include <dev/rndvar.h>
#include <dev/rndioctl.h>

#ifdef	RNDEBUG
int	rnd_debug = 0x0000;
#define	RD_INPUT	0x000f	/* input data */
#define	RD_OUTPUT	0x00f0	/* output data */
#define	RD_WAIT		0x0100	/* sleep/wakeup for good data */
#endif

/*
 * The pool is stirred with a primitive polynomial of degree 128
 * over GF(2), namely x^128 + x^99 + x^59 + x^31 + x^9 + x^7 + 1.
 * For a pool of size 64, try x^64+x^62+x^38+x^10+x^6+x+1.
 */
#define POOLBITS (POOLWORDS*32)
#if POOLWORDS == 2048
#define	TAP1	1638
#define	TAP2	1231
#define	TAP3	819
#define	TAP4	411
#define	TAP5	1
#elif POOLWORDS == 1024	/* also (819, 616, 410, 207, 2) */
#define	TAP1	817
#define	TAP2	615
#define	TAP3	412
#define	TAP4	204
#define	TAP5	1
#elif POOLWORDS == 512	/* also (409,307,206,102,2), (409,309,205,103,2) */
#define	TAP1	411
#define	TAP2	308
#define	TAP3	208
#define	TAP4	104
#define	TAP5	1
#elif POOLWORDS == 256
#define	TAP1	205
#define	TAP2	155
#define	TAP3	101
#define	TAP4	52
#define	TAP5	1
#elif POOLWORDS == 128	/* also (103, 78, 51, 27, 2) */
#define	TAP1	103
#define	TAP2	76
#define	TAP3	51
#define	TAP4	25
#define	TAP5	1
#elif POOLWORDS == 64
#define	TAP1	52
#define	TAP2	39
#define	TAP3	26
#define	TAP4	14
#define	TAP5	1
#elif POOLWORDS == 32
#define	TAP1	26
#define	TAP2	20
#define	TAP3	14
#define	TAP4	7
#define	TAP5	1
#else
#error No primitive polynomial available for chosen POOLWORDS
#endif

/*
 * For the purposes of better mixing, we use the CRC-32 polynomial as
 * well to make a twisted Generalized Feedback Shift Reigster
 *
 * (See M. Matsumoto & Y. Kurita, 1992.  Twisted GFSR generators.  ACM
 * Transactions on Modeling and Computer Simulation 2(3):179-194.
 * Also see M. Matsumoto & Y. Kurita, 1994.  Twisted GFSR generators
 * II.  ACM Transactions on Mdeling and Computer Simulation 4:254-266)
 *
 * Thanks to Colin Plumb for suggesting this.
 *
 * We have not analyzed the resultant polynomial to prove it primitive;
 * in fact it almost certainly isn't.  Nonetheless, the irreducible factors
 * of a random large-degree polynomial over GF(2) are more than large enough
 * that periodicity is not a concern.
 *
 * The input hash is much less sensitive than the output hash.  All
 * that we want of it is that it be a good non-cryptographic hash;
 * i.e. it not produce collisions when fed "random" data of the sort
 * we expect to see.  As long as the pool state differs for different
 * inputs, we have preserved the input entropy and done a good job.
 * The fact that an intelligent attacker can construct inputs that
 * will produce controlled alterations to the pool's state is not
 * important because we don't consider such inputs to contribute any
 * randomness.  The only property we need with respect to them is that
 * the attacker can't increase his/her knowledge of the pool's state.
 * Since all additions are reversible (knowing the final state and the
 * input, you can reconstruct the initial state), if an attacker has
 * any uncertainty about the initial state, he/she can only shuffle
 * that uncertainty about, but never cause any collisions (which would
 * decrease the uncertainty).
 *
 * The chosen system lets the state of the pool be (essentially) the input
 * modulo the generator polymnomial.  Now, for random primitive polynomials,
 * this is a universal class of hash functions, meaning that the chance
 * of a collision is limited by the attacker's knowledge of the generator
 * polynomail, so if it is chosen at random, an attacker can never force
 * a collision.  Here, we use a fixed polynomial, but we *can* assume that
 * ###--> it is unknown to the processes generating the input entropy. <-###
 * Because of this important property, this is a good, collision-resistant
 * hash; hash collisions will occur no more often than chance.
 */

/* pIII/333 reported to have some drops w/ these numbers */
#define QEVLEN 96
#define QEVSLOW 64 /* yet another 0.75 for 60-minutes hour /-; */
#define QEVSBITS 12

/* There is actually only one of these, globally. */
struct random_bucket {
	u_int	add_ptr;
	u_int	entropy_count;
	u_char	input_rotate;
	u_int32_t pool[POOLWORDS];
	u_int	asleep;
	u_int	queued;
	u_int	tmo;
};

/* There is one of these per entropy source */
struct timer_rand_state {
	u_int	last_time;
	u_int	last_delta;
	u_int	last_delta2;
	u_char	dont_count_entropy : 1;
	u_char	max_entropy : 1;
};

struct arc4_stream {
	u_int8_t s[256];
	u_int	cnt;
	u_int8_t i;
	u_int8_t j;
};

struct rand_event {
	struct rand_event *re_next;
	struct timer_rand_state *re_state;
	u_char re_nbits;
	u_int re_time;
	u_int re_val;
};

struct timeout rnd_timeout, arc4_timeout;
struct random_bucket random_state;
struct arc4_stream arc4random_state;
struct timer_rand_state rnd_states[RND_SRC_NUM];
struct rand_event rnd_event_space[QEVLEN];
struct rand_event *rnd_event_q;
struct rand_event *rnd_event_free;

int rnd_attached;
int arc4random_initialized;
struct rndstats rndstats;

static __inline u_int32_t roll(u_int32_t w, int i)
{
#ifdef i386
	__asm ("roll %%cl, %0" : "+r" (w) : "c" (i));
#else
	w = (w << i) | (w >> (32 - i));
#endif
	return w;
}

void dequeue_randomness __P((void *));

static __inline void add_entropy_words __P((const u_int32_t *, u_int n));
static __inline void extract_entropy __P((register u_int8_t *, int));

static __inline u_int8_t arc4_getbyte __P((void));
void arc4_stir __P((void));
void arc4_reinit __P((void *v));

/* Arcfour random stream generator.  This code is derived from section
 * 17.1 of Applied Cryptography, second edition, which describes a
 * stream cipher allegedly compatible with RSA Labs "RC4" cipher (the
 * actual description of which is a trade secret).  The same algorithm
 * is used as a stream cipher called "arcfour" in Tatu Ylonen's ssh
 * package.
 *
 * The initialization function here has been modified not to discard
 * old state, and its input always includes the time of day in
 * microseconds.  Moreover, bytes from the stream may at any point be
 * diverted to multiple processes or even kernel functions desiring
 * random numbers.  This increases the strenght of the random stream,
 * but makes it impossible to use this code for encryption--There is
 * no way ever to reproduce the same stream of random bytes.
 *
 * RC4 is a registered trademark of RSA Laboratories.
 */

void
arc4_stir(void)
{
	u_int8_t buf[256];
	register u_int8_t si;
	register int n, s;
	int len;

	microtime((struct timeval *) buf);
	len = random_state.entropy_count / 8; /* XXX maybe a half? */
	if (len > sizeof(buf) - sizeof(struct timeval))
		len = sizeof(buf) - sizeof(struct timeval);
	get_random_bytes(buf + sizeof (struct timeval), len);
	len += sizeof(struct timeval);

	s = splhigh();
	arc4random_state.i--;
	for (n = 0; n < 256; n++) {
		arc4random_state.i++;
		si = arc4random_state.s[arc4random_state.i];
		arc4random_state.j += si + buf[n % len];
		arc4random_state.s[arc4random_state.i] =
		    arc4random_state.s[arc4random_state.j];
		arc4random_state.s[arc4random_state.j] = si;
	}
	arc4random_state.j = arc4random_state.i;
	arc4random_state.cnt = 0;
	rndstats.arc4_stirs += len;
	rndstats.arc4_nstirs++;
	splx(s);
}

static __inline u_int8_t
arc4_getbyte(void)
{
	register u_int8_t si, sj;

	rndstats.arc4_reads++;
	arc4random_state.cnt++;
	arc4random_state.i++;
	si = arc4random_state.s[arc4random_state.i];
	arc4random_state.j += si;
	sj = arc4random_state.s[arc4random_state.j];
	arc4random_state.s[arc4random_state.i] = sj;
	arc4random_state.s[arc4random_state.j] = si;
	return arc4random_state.s[(si + sj) & 0xff];
}

static __inline void
arc4maybeinit(void)
{
	if (!arc4random_initialized) {
		arc4random_initialized++;
		arc4_stir();
	}
}

void
arc4_reinit(v)
	void *v;
{
	extern int hz;

	arc4random_initialized = 0;
	timeout_add(&arc4_timeout, 10*60*hz);
}

int
arc4random_8(void)
{
	arc4maybeinit();
	return arc4_getbyte();
}

u_int32_t
arc4random(void)
{
	arc4maybeinit();
	return ((arc4_getbyte() << 24) | (arc4_getbyte() << 16)
		| (arc4_getbyte() << 8) | arc4_getbyte());
}

void
randomattach(void)
{
	int i;
	struct timeval tv;
	struct rand_event *rep;

	if (rnd_attached) {
#ifdef RNDEBUG
		printf("random: second attach\n");
#endif
		return;
	}

	random_state.add_ptr = 0;
	random_state.entropy_count = 0;
	rnd_states[RND_SRC_TIMER].dont_count_entropy = 1;
	rnd_states[RND_SRC_TRUE].dont_count_entropy = 1;
	rnd_states[RND_SRC_TRUE].max_entropy = 1;

	bzero(&rndstats, sizeof(rndstats));
	bzero(&rnd_event_space, sizeof(rnd_event_space));
	rnd_event_free = rnd_event_space;
	for (rep = rnd_event_space; rep < &rnd_event_space[QEVLEN-1]; rep++)
		rep->re_next = rep + 1;
	for (i = 0; i < 256; i++)
		arc4random_state.s[i] = i;
	microtime(&tv);
	timeout_set(&rnd_timeout, dequeue_randomness, NULL);
	timeout_set(&arc4_timeout, arc4_reinit, NULL);
	arc4_reinit(NULL);
	rnd_attached = 1;
}

int
randomopen(dev, flag, mode, p)
	dev_t	dev;
	int	flag;
	int	mode;
	struct proc *p;
{
	return (minor (dev) < RND_NODEV) ? 0 : ENXIO;
}

int
randomclose(dev, flag, mode, p)
	dev_t	dev;
	int	flag;
	int	mode;
	struct proc *p;
{
	return 0;
}

/*
 * This function adds a byte into the entropy "pool".  It does not
 * update the entropy estimate.  The caller must do this if appropriate.
 *
 * The pool is stirred with a primitive polynomial of degree 128
 * over GF(2), namely x^128 + x^99 + x^59 + x^31 + x^9 + x^7 + 1.
 * For a pool of size 64, try x^64+x^62+x^38+x^10+x^6+x+1.
 *
 * We rotate the input word by a changing number of bits, to help
 * assure that all bits in the entropy get toggled.  Otherwise, if we
 * consistently feed the entropy pool small numbers (like jiffies and
 * scancodes, for example), the upper bits of the entropy pool don't
 * get affected. --- TYT, 10/11/95
 */
static __inline void
add_entropy_words(buf, n)
	const u_int32_t *buf;
	u_int n;
{
	static const u_int32_t twist_table[8] = {
		0x00000000, 0x3b6e20c8, 0x76dc4190, 0x4db26158,
		0xedb88320, 0xd6d6a3e8, 0x9b64c2b0, 0xa00ae278
	};
	u_int i;
	int new_rotate;
	u_int32_t w;

	while (n--) {
		w = roll(*buf, random_state.input_rotate);
		i = random_state.add_ptr =
		    (random_state.add_ptr - 1) & (POOLWORDS - 1);
		/*
		 * Normally, we add 7 bits of rotation to the pool.
		 * At the beginning of the pool, add an extra 7 bits
		 * rotation, so that successive passes spread the
		 * input bits across the pool evenly.
		 */
		new_rotate = random_state.input_rotate + 14;
		if (i)
			new_rotate = random_state.input_rotate + 7;
		random_state.input_rotate = new_rotate & 31;

		/* XOR in the various taps */
		w ^= random_state.pool[(i+TAP1)&(POOLWORDS-1)];
		w ^= random_state.pool[(i+TAP2)&(POOLWORDS-1)];
		w ^= random_state.pool[(i+TAP3)&(POOLWORDS-1)];
		w ^= random_state.pool[(i+TAP4)&(POOLWORDS-1)];
		w ^= random_state.pool[(i+TAP5)&(POOLWORDS-1)];
		w ^= random_state.pool[i];
		random_state.pool[i] = (w >> 3) ^ twist_table[w & 7];
	}
}

/*
 * This function adds entropy to the entropy "pool" by using timing
 * delays.  It uses the timer_rand_state structure to make an estimate
 * of how many bits of entropy this call has added to the pool.
 *
 * The number "num" is also added to the pool - it should somehow describe
 * the type of event which just happened.  This is currently 0-255 for
 * keyboard scan codes, and 256 upwards for interrupts.
 * On the i386, this is assumed to be at most 16 bits, and the high bits
 * are used for a high-resolution timer.
 *
 */
void
enqueue_randomness(state, val)
	int	state, val;
{
	struct timer_rand_state *p;
	struct timeval	tv;
	register struct rand_event *rep;
	int s;
	u_int	time, nbits;

#ifdef DIAGNOSTIC
	if (state < 0 || state >= RND_SRC_NUM)
		return;
#endif

	p = &rnd_states[state];
	val += state << 13;

	microtime(&tv);
	time = tv.tv_usec ^ tv.tv_sec;
	nbits = 0;

	/*
	 * Calculate number of bits of randomness we probably
	 * added.  We take into account the first and second order
	 * deltas in order to make our estimate.
	 */
	if (!p->dont_count_entropy) {
		register int	delta, delta2, delta3;
		delta  = time   - p->last_time;
		delta2 = delta  - p->last_delta;
		delta3 = delta2 - p->last_delta2;

		if (delta < 0) delta = -delta;
		if (delta2 < 0) delta2 = -delta2;
		if (delta3 < 0) delta3 = -delta3;
		if (delta > delta2) delta = delta2;
		if (delta > delta3) delta = delta3;
		delta3 = delta >>= 1;
		/*
		 * delta &= 0xfff;
		 * we don't do it since our time sheet is different from linux
		 */

		if (delta & 0xffff0000) {
			nbits = 16;
			delta >>= 16;
		}
		if (delta & 0xff00) {
			nbits += 8;
			delta >>= 8;
		}
		if (delta & 0xf0) {
			nbits += 4;
			delta >>= 4;
		}
		if (delta & 0xc) {
			nbits += 2;
			delta >>= 2;
		}
		if (delta & 2) {
			nbits += 1;
			delta >>= 1;
		}
		if (delta & 1)
			nbits++;

		/*
		 * the logic is to drop low-entropy entries,
		 * in hope for dequeuing to be more sourcefull
		 */
		if (random_state.queued > QEVSLOW && nbits < QEVSBITS) {
			rndstats.rnd_drople++;
			return;
		}
		p->last_time = time;
		p->last_delta  = delta3;
		p->last_delta2 = delta2;
	} else if (p->max_entropy)
		nbits = 8 * sizeof(val) - 1;

	s = splhigh();
	if ((rep = rnd_event_free) == NULL) {
		rndstats.rnd_drops++;
		splx(s);
		return;
	}
	rnd_event_free = rep->re_next;

	rep->re_state = p;
	rep->re_nbits = nbits;
	rep->re_time = time;
	rep->re_val = val;

	rep->re_next = rnd_event_q;
	rnd_event_q = rep;
	rep = rep->re_next;
	random_state.queued++;

	rndstats.rnd_enqs++;
	rndstats.rnd_ed[nbits]++;
	rndstats.rnd_sc[state]++;
	rndstats.rnd_sb[state] += nbits;

	if (++random_state.queued > QEVSLOW/2 && !random_state.tmo) {
		random_state.tmo++;
		timeout_add(&rnd_timeout, 1);
	}
	splx(s);
}

void
dequeue_randomness(v)
	void *v;
{
	register struct rand_event *rep;
	u_int32_t val, time;
	u_int nbits;
	int s;

	timeout_del(&rnd_timeout);
	rndstats.rnd_deqs++;

	do {
		s = splhigh();
		if (rnd_event_q == NULL) {
			splx(s);
			break;
		}
		rep = rnd_event_q;
		rnd_event_q = rep->re_next;
		random_state.queued--;

		val = rep->re_val;
		time = rep->re_time;
		nbits = rep->re_nbits;

		rep->re_next = rnd_event_free;
		rnd_event_free = rep;
		splx(s);

		add_entropy_words(&val, 1);
		add_entropy_words(&time, 1);

		rndstats.rnd_total += nbits;
		random_state.entropy_count += nbits;
		if (random_state.entropy_count > POOLBITS)
			random_state.entropy_count = POOLBITS;

		if (random_state.entropy_count > 8 &&
		    random_state.asleep != 0) {
#ifdef	RNDEBUG
			if (rnd_debug & RD_WAIT)
				printf("rnd: wakeup[%u]{%u}\n",
				    random_state.asleep,
				    random_state.entropy_count);
#endif
			random_state.asleep--;
			wakeup(&random_state.asleep);
		}
	} while(1);

	random_state.tmo = 0;
}

#if POOLWORDS % 16
#error extract_entropy() assumes that POOLWORDS is a multiple of 16 words.
#endif

/*
 * This function extracts randomness from the "entropy pool", and
 * returns it in a buffer.  This function computes how many remaining
 * bits of entropy are left in the pool, but it does not restrict the
 * number of bytes that are actually obtained.
 */
static __inline void
extract_entropy(buf, nbytes)
	register u_int8_t *buf;
	int	nbytes;
{
	MD5_CTX tmp;
	u_char buffer[16];

	add_timer_randomness(nbytes);

	/* Redundant, but just in case... */
	if (random_state.entropy_count > POOLBITS)
		random_state.entropy_count = POOLBITS;

	if (random_state.entropy_count / 8 > nbytes)
		random_state.entropy_count -= nbytes*8;
	else
		random_state.entropy_count = 0;

	while (nbytes) {
		register u_char *p = buf;
		register int i = sizeof(buffer);

		if (i > nbytes) {
			i = nbytes;
			p = buffer;
		}

		/* Hash the pool to get the output */
		MD5Init(&tmp);
		MD5Update(&tmp, (u_int8_t*)random_state.pool,
		    sizeof(random_state.pool));
		MD5Final(p, &tmp);

		/*
		 * In case the hash function has some recognizable
		 * output pattern, we fold it in half.
		 */
		p[0] ^= p[15];
		p[1] ^= p[14];
		p[2] ^= p[13];
		p[3] ^= p[12];
		p[4] ^= p[11];
		p[5] ^= p[10];
		p[6] ^= p[ 9];
		p[7] ^= p[ 8];

		/* Modify pool so next hash will produce different results */
		add_entropy_words((u_int32_t*)p, sizeof(buffer)/4);

		/* Copy data to destination buffer */
		if (i < sizeof(buffer))
			bcopy(buffer, buf, i);
		nbytes -= i;
		buf += i;
		add_timer_randomness(nbytes);
	}

	/* Wipe data from memory */
	bzero(&tmp, sizeof(tmp));
	bzero(&buffer, sizeof(buffer));
}

/*
 * This function is the exported kernel interface.  It returns some
 * number of good random numbers, suitable for seeding TCP sequence
 * numbers, etc.
 */
void
get_random_bytes(buf, nbytes)
	void	*buf;
	size_t	nbytes;
{
	extract_entropy((u_int8_t *) buf, nbytes);
	rndstats.rnd_used += nbytes * 8;
}

int
randomread(dev, uio, ioflag)
	dev_t	dev;
	struct uio *uio;
	int	ioflag;
{
	int	ret = 0;
	int	s, i;

	if (uio->uio_resid == 0)
		return 0;

	while (!ret && uio->uio_resid > 0) {
		u_int32_t buf[ POOLWORDS ];
		int	n = min(sizeof(buf), uio->uio_resid);

		s = splhigh();
		switch(minor(dev)) {
		case RND_RND:
			ret = EIO;	/* no chip -- error */
			break;
		case RND_SRND:
			if (random_state.entropy_count < 16 * 8) {
				if (ioflag & IO_NDELAY) {
					ret = EWOULDBLOCK;
					break;
				}
#ifdef	RNDEBUG
				if (rnd_debug & RD_WAIT)
					printf("rnd: sleep[%u]\n",
					    random_state.asleep);
#endif
				random_state.asleep++;
				rndstats.rnd_waits++;
				ret = tsleep(&random_state.asleep,
				    PWAIT | PCATCH, "rndrd", 0);
#ifdef	RNDEBUG
				if (rnd_debug & RD_WAIT)
					printf("rnd: awakened(%d)\n", ret);
#endif
				if (ret)
					break;
			}
			if (n > random_state.entropy_count / 8)
				n = random_state.entropy_count / 8;
			rndstats.rnd_reads++;
#ifdef	RNDEBUG
			if (rnd_debug & RD_OUTPUT)
				printf("rnd: %u possible output\n", n);
#endif
		case RND_URND:
			get_random_bytes((char *)buf, n);
#ifdef	RNDEBUG
			if (rnd_debug & RD_OUTPUT)
				printf("rnd: %u bytes for output\n", n);
#endif
			break;
		case RND_PRND:
			i = (n + 3) / 4;
			while (i--)
				buf[i] = random() << 16 | (random() & 0xFFFF);
			break;
		case RND_ARND:
		{
			u_int8_t *cp = (u_int8_t *) buf;
			u_int8_t *end = cp + n;
			while (cp < end)
				*cp++ = arc4random_8();
			break;
		}
		default:
			ret = ENXIO;
		}
		splx(s);
		if (n != 0 && ret == 0)
			ret = uiomove((caddr_t)buf, n, uio);
	}
	return ret;
}

int
randomselect(dev, rw, p)
	dev_t	dev;
	int	rw;
	struct proc *p;
{
	switch (rw) {
	case FREAD:
		return random_state.entropy_count > 0;
	case FWRITE:
		return 1;
	}
	return 0;
}

int
randomwrite(dev, uio, flags)
	dev_t	dev;
	struct uio *uio;
	int	flags;
{
	int	ret = 0;

	if (minor(dev) == RND_RND || minor(dev) == RND_PRND)
		return ENXIO;

	if (uio->uio_resid == 0)
		return 0;

	while (!ret && uio->uio_resid > 0) {
		u_int32_t	buf[ POOLWORDS ];
		u_short		n = min(sizeof(buf),uio->uio_resid);

		ret = uiomove((caddr_t)buf, n, uio);
		if (!ret) {
			while (n % sizeof(u_int32_t))
				((u_int8_t *) buf)[n++] = 0;
			add_entropy_words(buf, n / 4);
		}
	}

	if (minor(dev) == RND_ARND && !ret)
		arc4random_initialized = 0;

	return ret;
}

int
randomioctl(dev, cmd, data, flag, p)
	dev_t	dev;
	u_long	cmd;
	caddr_t	data;
	int	flag;
	struct proc *p;
{
	int	ret = 0;
	u_int	cnt;

	add_timer_randomness((u_long)p ^ (u_long)data ^ cmd);

	switch (cmd) {
	case FIOASYNC:
		/* rnd has no async flag in softc so this is really a no-op. */
		break;

	case FIONBIO:
		/* Handled in the upper FS layer. */
		break;

	case RNDGETENTCNT:
		ret = copyout(&random_state.entropy_count, data,
		    sizeof(random_state.entropy_count));
		break;
	case RNDADDTOENTCNT:
		if (suser(p->p_ucred, &p->p_acflag) != 0)
			ret = EPERM;
		else {
			copyin(&cnt, data, sizeof(cnt));
			random_state.entropy_count += cnt;
			if (random_state.entropy_count > POOLBITS)
				random_state.entropy_count = POOLBITS;
		}
		break;
	case RNDZAPENTCNT:
		if (suser(p->p_ucred, &p->p_acflag) != 0)
			ret = EPERM;
		else
			random_state.entropy_count = 0;
		break;
	case RNDSTIRARC4:
		if (suser(p->p_ucred, &p->p_acflag) != 0)
			ret = EPERM;
		else if (random_state.entropy_count < 64)
			ret = EAGAIN;
		else
			arc4random_initialized = 0;
		break;
	case RNDCLRSTATS:
		if (suser(p->p_ucred, &p->p_acflag) != 0)
			ret = EPERM;
		else
			bzero(&rndstats, sizeof(rndstats));
		break;
	default:
		ret = EINVAL;
	}

	add_timer_randomness((u_long)p ^ (u_long)data ^ cmd);
	return ret;
}
