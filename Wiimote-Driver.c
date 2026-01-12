#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/hid.h>
#include <linux/input.h>
#include <linux/string.h>
#include <linux/init.h>

static const struct hid_device_id wiimote_devices[] = {
	{ HID_BLUETOOTH_DEVICE(0x057e, 0x0306) }, /* Original Wii remote */
	{ HID_BLUETOOTH_DEVICE(0x057e, 0x0330) }, /* Wii U compatible wiimote */
}

MODULE_DEVICE_TABLE(hid, wiimote_devices);

struct my_wiimote {
	struct hid_device *hdev;
	struct input_dev *input;
};

static int my_wiimote_raw_event(struct hid_device *hdev, struct hid_report *report, u8 *data, int size)
{
	struct my_wiimote *wiimote = hid_get_drvdata(hdev);
	u16 buttons;

	/* core button report 0x30 */
	if (data[0] != 0x30)
		return 0;
	
	/* bit shifting to put button data into buttons u16 */
	buttons = (data[1] << 8) | data[2];

	input_report_key(wiimote->input, KEY_A, !(buttons & 0x0008));
	input_report_key(wiimote->input, KEY_B, !(buttons & 0x0004));
	input_report_key(wiimote->input, KEY_HOME, !(buttons & 0x0080));
	
	input_sync(wiimote->input);
	
	/* return 1 to stop HID core from trying to also process this report */
	return 1;
}


static int __init my_init(void) {
	printk("Wiimote-Driver - Hello from the linux kernel!\n");
	return 0;
}

static void __exit my_exit(void) {
	printk("Wiimote-Driver - Goodbye from the linux kernel!\n");
}

module_init(my_init);
module_exit(my_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Joshua Lowe");
MODULE_DESCRIPTION("A device driver for a wiimote created as a learning project");

