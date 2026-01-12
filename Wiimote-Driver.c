#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/hid.h>
#include <linux/input.h>
#include <linux/string.h>
#include <linux/init.h>

static const struct hid_device_id wiimote_devices[] = {
	{ HID_BLUETOOTH_DEVICE(0x057e, 0x0306) }, /* Original Wii remote */
	{ HID_BLUETOOTH_DEVICE(0x057e, 0x0330) }, /* Wii U compatible wiimote */
	{ } /* empty entry signifies end of array to kernel */
};

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

static int my_wiimote_probe(struct hid_device *hdev, const struct hid_device_id *id)
{
	struct my_wiimote *wiimote;
	int ret;
	
	/* sanity check */
	if (strstr(hdev->name, "Nintendo"))
		return -ENODEV;

	/* devm_kzalloc does a lot of heavy lifting for memory management
	 * with this the memory is tied to the lifetime of the device
	 * it is also zero filled
	 */
	wiimote = devm_kzalloc(&hdev->dev, sizeof(*wiimote), GFP_KERNEL);

	wiimote->hdev = hdev;
	hid_set_drvdata(hdev, wiimote);

	ret = hid_parse(hdev);
	if (ret) {
		hid_err(hdev, "Wiimote-Driver - hid_parse failed\n");
		return ret;
	}

	/* start HID device telling kernel that it is HID raw - let us
	 * handle the input ourselves without trying to claim it first
	 */
	ret = hid_hw_start(hdev, HID_CONNECT_HIDRAW);
	if (ret) {
		hid_err(hdev, "Wiimote-Driver - hid_hw_start failed\n");
		return ret;
	}
	
	/* create an input device for the wiimote. this is what the user
	 * will interact with, and so it is important to fill in data to
	 * not look generic
	 */
	wiimote->input = devm_input_allocate_device(&hdev->dev);
	if (!wiimote->input)
		return -ENOMEM;

	wiimote->input->name = "My Wiimote";
	wiimote->input->phys = hdev->phys;
	wiimote->input->id.bustype = BUS_BLUETOOTH;
	wiimote->input->id.vendor = 0x057e;
	wiimote->input->id.product = hdev->product;
	wiimote->input->id.version = hdev->version;
	
	/* input system must know the buttons that can potentially
	 * come up on this device up from, so they are declared here
	 */
	__set_bit(EV_KEY, wiimote->input->evbit);
	__set_bit(KEY_A, wiimote->input->keybit);
	__set_bit(KEY_B, wiimote->input->keybit);
	__set_bit(KEY_HOME, wiimote->input->keybit);
	
	/* register input device here */
	ret = input_register_device(wiimote->input);
	if (ret) {
		hid_err(hdev, "Wiimote-Driver - input_register_device failed\n");
		return ret;
	}

	hid_info(hdev, "Wiimote-Driver - Eductional wiimote driver attached\n");
	return 0;
}

static void my_wiimote_remove(struct hid_device *hdev)
{
	hid_hw_stop(hdev);
	hid_info(hdev, "Wiimote-Driver - my wiimote driver has been removed\n");
}

static struct hid_driver my_wiimote_driver = {
	.name = "hid-my-wiimote",
	.id_table = wiimote_devices,
	.probe = my_wiimote_probe,
	.remove = my_wiimote_remove,
	.raw_event = my_wiimote_raw_event,
};

module_hid_driver(my_wiimote_driver);

/* Module metadata */

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Joshua Lowe");
MODULE_DESCRIPTION("A device driver for a wiimote created as a learning project");

