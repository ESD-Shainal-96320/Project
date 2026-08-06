/*
 * max7219_ldisc.c - BeagleBone Black combined driver: keeps the
 * existing MAX7219 SPI probe/init logic, and adds a TTY line
 * discipline that attaches to /dev/ttyGS0 (USB gadget serial) and
 * writes incoming CPU/TEMP values straight to the display - entirely
 * in-kernel, no userspace program needed once attached.
 *
 * Wire protocol from the PC: one line per update, two comma-separated
 * integers, values pre-scaled x10 (since kernel code has no floats):
 *     <cpu_x10>,<temp_x10>\n     e.g. "160,450\n" = 16.0, 45.0
 *
 * Attaching: this module only registers the line discipline and the
 * SPI driver - it does NOT attach itself to ttyGS0 automatically,
 * since that requires an open file descriptor on that tty (standard
 * Linux requirement, same as pppd/ldattach). Use the tiny companion
 * program ldattach_max7219.c once after insmod:
 *     sudo ./ldattach_max7219 /dev/ttyGS0
 * That program does nothing but the attach call, then idles - all
 * actual parsing/display logic below runs in the kernel from then on.
 *
 * Build:
 *   sudo make
 *   sudo insmod max7219_driver.ko
 *   sudo ./ldattach_max7219 /dev/ttyGS0    (one-time, see above)
 *   sudo ldattach 29 /dev/ttyGS0
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/spi/spi.h>
#include <linux/of.h>
#include <linux/tty.h>
#include <linux/tty_ldisc.h>
#include <linux/mutex.h>
#include <linux/slab.h>

#define DRIVER_NAME   "max7219_ldisc"
#define NUM_DIGITS    8

/* Pick an unused line discipline number. N_TTY=0 and a handful of
 * others (SLIP, PPP, etc.) are reserved by the kernel; NR_LDISCS
 * (usually 30) is the ceiling. 29 is typically free, but if
 * registration fails with -EINVAL/-EBUSY, try a different number. */
#define N_MAX7219 29

/* MAX7219 register map */
#define REG_DIGIT0       0x01
#define REG_DECODEMODE   0x09
#define REG_INTENSITY    0x0A
#define REG_SCANLIMIT    0x0B
#define REG_SHUTDOWN     0x0C
#define REG_DISPLAYTEST  0x0F

/* Raw 7-segment patterns, bit order DP A B C D E F G */
#define SEG_0     0x7E
#define SEG_1     0x30
#define SEG_2     0x6D
#define SEG_3     0x79
#define SEG_4     0x33
#define SEG_5     0x5B
#define SEG_6     0x5F
#define SEG_7     0x70
#define SEG_8     0x7F
#define SEG_9     0x7B
#define SEG_C     0x4E
#define SEG_T     0x0F
#define SEG_BLANK 0x00
#define SEG_DP    0x80

static const u8 digitTable[10] = {
	SEG_0, SEG_1, SEG_2, SEG_3, SEG_4,
	SEG_5, SEG_6, SEG_7, SEG_8, SEG_9
};

/* --- SPI / MAX7219 side (same as max7219_driver.c) --------------------- */

struct max7219_priv {
	struct spi_device *spi;
	struct mutex       lock;
};

/* Only one MAX7219 device is expected; the ldisc callback (which has
 * no natural link back to a specific spi_device) uses this global to
 * reach it. Fine for a single-display setup like this project. */
static struct max7219_priv *g_max7219;

static int max7219_reg_write(struct max7219_priv *priv, u8 reg, u8 data)
{
	u8 tx[2] = { reg, data };

	return spi_write(priv->spi, tx, sizeof(tx));
}

static int max7219_init(struct max7219_priv *priv)
{
	int ret, i;

	ret = max7219_reg_write(priv, REG_SHUTDOWN, 0x01);
	if (ret) return ret;
	ret = max7219_reg_write(priv, REG_DISPLAYTEST, 0x00);
	if (ret) return ret;
	ret = max7219_reg_write(priv, REG_DECODEMODE, 0x00); /* raw segments */
	if (ret) return ret;
	ret = max7219_reg_write(priv, REG_SCANLIMIT, 0x07);
	if (ret) return ret;
	ret = max7219_reg_write(priv, REG_INTENSITY, 0x08);
	if (ret) return ret;

	for (i = 0; i < NUM_DIGITS; i++) {
		ret = max7219_reg_write(priv, REG_DIGIT0 + i, SEG_BLANK);
		if (ret) return ret;
	}
	return 0;
}

/* visual position 0 = rightmost, 7 = leftmost (matches earlier fix) */
static int set_digit_at_visual_pos(struct max7219_priv *priv, int pos, u8 seg)
{
	u8 reg = REG_DIGIT0 + (NUM_DIGITS - 1 - pos);

	return max7219_reg_write(priv, reg, seg);
}

/* Displays one metric across 4 visual positions: [label][tens][ones+DP][tenths] */
static int display_labeled_value(struct max7219_priv *priv, int pos_label,
				  char label, int value_x10)
{
	int whole, tenths, tens, ones;
	u8 label_seg;

	if (value_x10 < 0)
		value_x10 = 0;
	if (value_x10 > 999)
		value_x10 = 999;

	whole  = value_x10 / 10;
	tenths = value_x10 % 10;
	tens   = (whole / 10) % 10;
	ones   = whole % 10;

	label_seg = (label == 'C') ? SEG_C :
		    (label == 'T') ? SEG_T : SEG_BLANK;

	if (set_digit_at_visual_pos(priv, pos_label,     label_seg))            return -1;
	if (set_digit_at_visual_pos(priv, pos_label - 1, digitTable[tens]))      return -1;
	if (set_digit_at_visual_pos(priv, pos_label - 2, digitTable[ones] | SEG_DP)) return -1;
	if (set_digit_at_visual_pos(priv, pos_label - 3, digitTable[tenths]))    return -1;

	return 0;
}

static void max7219_show_stats(int cpu_x10, int temp_x10)
{
	if (!g_max7219)
		return;

	mutex_lock(&g_max7219->lock);
	display_labeled_value(g_max7219, 7, 'C', cpu_x10);
	display_labeled_value(g_max7219, 3, 'T', temp_x10);
	mutex_unlock(&g_max7219->lock);
}

/* --- TTY line discipline side ------------------------------------------ */

#define LDISC_LINEBUF_SIZE 64

struct max7219_ldisc_data {
	char   linebuf[LDISC_LINEBUF_SIZE];
	size_t linelen;
};

static int max7219_ldisc_open(struct tty_struct *tty)
{
	struct max7219_ldisc_data *ld;

	ld = kzalloc(sizeof(*ld), GFP_KERNEL);
	if (!ld)
		return -ENOMEM;

	tty->disc_data = ld;
	pr_info("max7219_ldisc: attached to %s\n", tty->name);
	return 0;
}

static void max7219_ldisc_close(struct tty_struct *tty)
{
	struct max7219_ldisc_data *ld = tty->disc_data;

	pr_info("max7219_ldisc: detached from %s\n", tty->name);
	kfree(ld);
	tty->disc_data = NULL;
}

static void max7219_ldisc_process_line(const char *line)
{
	int cpu_x10 = 0, temp_x10 = 0;

	/* kernel-provided sscanf, same idea as libc's */
	if (sscanf(line, "%d,%d", &cpu_x10, &temp_x10) == 2)
		max7219_show_stats(cpu_x10, temp_x10);
	else
		pr_warn("max7219_ldisc: unparsed line: '%s'\n", line);
}

static int max7219_ldisc_receive_buf2(struct tty_struct *tty,
				       const unsigned char *cp,
				       char *fp, int count)
{
	pr_info("receive_buf2 called count=%d\n", count);
	struct max7219_ldisc_data *ld = tty->disc_data;
	int i;

	if (!ld)
		return count;

	for (i = 0; i < count; i++) {
		char c = cp[i];

		if (c == '\n') {
			ld->linebuf[ld->linelen] = '\0';
			max7219_ldisc_process_line(ld->linebuf);
			ld->linelen = 0;
		} else if (c != '\r' && ld->linelen < LDISC_LINEBUF_SIZE - 1) {
			ld->linebuf[ld->linelen++] = c;
		}
	}

	return count;
}

static struct tty_ldisc_ops max7219_ldisc_ops = {
	.owner        = THIS_MODULE,
	// .num          = N_MAX7219,
	.name         = "max7219",
	.open         = max7219_ldisc_open,
	.close        = max7219_ldisc_close,
	.receive_buf2 = max7219_ldisc_receive_buf2,
};

/* --- SPI probe/remove ---------------------------------------------------- */

static int max7219_probe(struct spi_device *spi)
{
	struct max7219_priv *priv;
	int ret;

	priv = devm_kzalloc(&spi->dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->spi = spi;
	mutex_init(&priv->lock);
	spi_set_drvdata(spi, priv);

	spi->mode = SPI_MODE_0;
	spi->bits_per_word = 8;
	ret = spi_setup(spi);
	if (ret) {
		dev_err(&spi->dev, "spi_setup failed: %d\n", ret);
		return ret;
	}

	ret = max7219_init(priv);
	if (ret) {
		dev_err(&spi->dev, "max7219_init failed: %d\n", ret);
		return ret;
	}

	g_max7219 = priv;
	dev_info(&spi->dev, "max7219_ldisc probed on cs%d\n", spi->chip_select);
	return 0;
}

static int max7219_remove(struct spi_device *spi)
{
	struct max7219_priv *priv = spi_get_drvdata(spi);

	g_max7219 = NULL;
	max7219_reg_write(priv, REG_SHUTDOWN, 0x00);
	return 0;
}

static const struct of_device_id max7219_of_match[] = {
	{ .compatible = "maxim,max7219" },
	{ }
};
MODULE_DEVICE_TABLE(of, max7219_of_match);

static const struct spi_device_id max7219_id[] = {
	{ "max7219", 0 },
	{ }
};
MODULE_DEVICE_TABLE(spi, max7219_id);

static struct spi_driver max7219_driver = {
	.driver = {
		.name           = DRIVER_NAME,
		.of_match_table = max7219_of_match,
	},
	.probe    = max7219_probe,
	.remove   = max7219_remove,
	.id_table = max7219_id,
};

/* --- module init/exit: register both the SPI driver and the ldisc ------- */

static int __init max7219_ldisc_init(void)
{
	int ret;

	ret = spi_register_driver(&max7219_driver);
	if (ret) {
		pr_err("max7219_ldisc: spi_register_driver failed: %d\n", ret);
		return ret;
	}

	ret = tty_register_ldisc(N_MAX7219,
                         &max7219_ldisc_ops);
	if (ret) {
		pr_err("max7219_ldisc: tty_register_ldisc failed: %d "
		       "(try a different N_MAX7219 number if -EINVAL/-EBUSY)\n", ret);
		spi_unregister_driver(&max7219_driver);
		return ret;
	}

	pr_info("max7219_ldisc: loaded, ldisc number %d. Attach with: "
		"ldattach_max7219 /dev/ttyGS0\n", N_MAX7219);
	return 0;
}

static void __exit max7219_ldisc_exit(void)
{
	tty_unregister_ldisc(N_MAX7219);
	spi_unregister_driver(&max7219_driver);
}

module_init(max7219_ldisc_init);
module_exit(max7219_ldisc_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Vivek Pawar <vivekpawar.eng@gmail.com>");
MODULE_DESCRIPTION("MAX7219 SPI driver + TTY line discipline for in-kernel USB-to-display bridging");
