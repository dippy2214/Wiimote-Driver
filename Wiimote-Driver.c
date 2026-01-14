#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/hid.h>
#include <linux/input.h>
#include <linux/string.h>
#include <linux/init.h>

#include "Wiimote-Driver-Flags.c"

/* set devices to be picked up by driver here */
static const struct hid_device_id wiimote_devices[] = {
	{ HID_BLUETOOTH_DEVICE(0x057E, 0x0306) }, /* Original Wii remote */
	{ HID_BLUETOOTH_DEVICE(0x057E, 0x0330) }, /* Wii U compatible wiimote */
	{ } /* empty entry signifies end of array to kernel */
};

MODULE_DEVICE_TABLE(hid, wiimote_devices);

/* This struct is used to store persisting info on each wiimote reference
 * it is mounted to the hid device as driver information and there is one
 * per wiimote
 */
struct my_wiimote {
	struct hid_device *hdev;
	struct input_dev *input;
	u8 report_mode;
	struct mutex lock;
};

/* Helper function to send data to wiimote - used mostly for settings */
static int wiimote_send(struct hid_device *hdev, u8 *buffer, int count)
{
	hid_info(hdev, "Wiimote-Driver - Sending message to wiimote!\n");
	u8 *buf;
	int ret;

	if (!hdev->ll_driver->output_report)
		return -ENODEV;

	/* copy data to new buffer as good practice in case old buffer gets reused */	
	buf = kmemdup(buffer, count, GFP_KERNEL);

	if (!buf)
		return -ENOMEM;

	ret = hid_hw_output_report(hdev, buf, count);

	kfree(buf);
	return ret;
}

/* Helper function to set wiimote report mode */
static int set_wiimote_report_mode(struct hid_device *hdev, u8 report_mode)
{
	struct my_wiimote *wiimote = hid_get_drvdata(hdev);
	mutex_lock(&wiimote->lock);
	wiimote->report_mode = report_mode;
	mutex_unlock(&wiimote->lock);
	int ret;
	u8 report_mode_message[3] = {
		0x12,
		0x00,
		report_mode,
	};

	int report_size = sizeof(*report_mode_message)/sizeof(report_mode_message[0]);
	ret = wiimote_send(hdev, report_mode_message, report_size);
	return ret;
}

/* sysfs function for wiimote report mode show*/
static ssize_t sysfs_report_mode_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct my_wiimote *wiimote = dev_get_drvdata(dev);

	return sysfs_emit(buf, "0x%02x\n", wiimote->report_mode);
}

/* sysfs function for wiimote report mode store */
static ssize_t sysfs_report_mode_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	struct my_wiimote *wiimote = dev_get_drvdata(dev);
	unsigned int mode;
	int ret;
	ret = kstrtouint(buf, 0, &mode);
	if (ret)
		return ret;
	
	/* validate allowed report modes */
	switch (mode)
	{
		case REPORT_BUTTONS:
		case REPORT_BUTTONS_ACCELEROMETER:
		case REPORT_BUTTONS_ACCELEROMETER_IRSENSOR:
			break;
		default:
			return -EINVAL;
	}

	set_wiimote_report_mode(wiimote->hdev, mode);
	
	return count;
}

static DEVICE_ATTR_RW(sysfs_report_mode);

/* HID raw event handler - communication logic with wiimote starts here */
static int my_wiimote_raw_event(struct hid_device *hdev, struct hid_report *report, u8 *data, int size)
{
	hid_info(hdev, "Wiimote-Driver - Raw event triggered!\n");

	struct my_wiimote *wiimote = hid_get_drvdata(hdev);
	u16 buttons;

	/* core button report hard coded for now */
	if (data[0] != REPORT_BUTTONS) {
		hid_info(hdev, "Wiimote-Driver - Report mode not yet supported!");
		return 0;
	}
	
	/* bit shifting to put button data into buttons u16 */
	buttons = (data[1] << 8) | data[2];

	input_report_key(wiimote->input, BTN_A, !(buttons & BITMASK_A));
	input_report_key(wiimote->input, BTN_B, !(buttons & BITMASK_B));
	input_report_key(wiimote->input, BTN_MODE, !(buttons & BITMASK_HOME));
	input_report_key(wiimote->input, BTN_START, !(buttons & BITMASK_START));
	input_report_key(wiimote->input, BTN_SELECT, !(buttons & BITMASK_SELECT));
	
	input_report_key(wiimote->input, BTN_DPAD_UP, !(buttons & BITMASK_DPAD_UP));
	input_report_key(wiimote->input, BTN_DPAD_DOWN, !(buttons & BITMASK_DPAD_DOWN));
	input_report_key(wiimote->input, BTN_DPAD_LEFT, !(buttons & BITMASK_DPAD_LEFT));
	input_report_key(wiimote->input, BTN_DPAD_RIGHT, !(buttons & BITMASK_DPAD_RIGHT));
	
	input_sync(wiimote->input);
	
	/* return 1 to stop HID core from trying to also process this report */
	return 1;
}

/* Probe runs immediately upon detecting a connection to a wiimote, and handles all
 * connection specific events such as initialisation of the device
 */ 
static int my_wiimote_probe(struct hid_device *hdev, const struct hid_device_id *id)
{
	hid_info(hdev, "Wiimote-Driver - probe function active");
	struct my_wiimote *wiimote;
	int ret;
	
	/* sanity check */
	if (!strstr(hdev->name, "Nintendo"))
		return -ENODEV;

	/* devm_kzalloc does a lot of heavy lifting for memory management
	 * with this the memory is tied to the lifetime of the device
	 * it is also zero filled
	 */
	wiimote = devm_kzalloc(&hdev->dev, sizeof(*wiimote), GFP_KERNEL);
	if (!wiimote)
		return -ENOMEM;

	wiimote->hdev = hdev;
	hid_set_drvdata(hdev, wiimote);

	wiimote->report_mode = REPORT_BUTTONS;
	mutex_init(&wiimote->lock);

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
	__set_bit(BTN_A, wiimote->input->keybit);
	__set_bit(BTN_B, wiimote->input->keybit);
	__set_bit(BTN_MODE, wiimote->input->keybit);
	__set_bit(BTN_START, wiimote->input->keybit);
	__set_bit(BTN_SELECT, wiimote->input->keybit);

	__set_bit(BTN_DPAD_UP, wiimote->input->keybit);
	__set_bit(BTN_DPAD_DOWN, wiimote->input->keybit);
	__set_bit(BTN_DPAD_LEFT, wiimote->input->keybit);
	__set_bit(BTN_DPAD_RIGHT, wiimote->input->keybit);
	
	/* register input device here */
	ret = input_register_device(wiimote->input);
	if (ret) {
		hid_err(hdev, "Wiimote-Driver - input_register_device failed\n");
		return ret;
	}
	
	ret = device_create_file(&hdev->dev, &dev_attr_sysfs_report_mode);
	if (ret) {
		hid_err(hdev, "failed to create report_mode sysfs file\n");
		return ret;
	}

	hid_info(hdev, "Wiimote-Driver - Wiimote driver attached to wiimote!\n");	

	set_wiimote_report_mode(hdev, wiimote->report_mode);

	return 0;
}

/* Remove is called when a wiimote is disconnected and handles any cleanup for when
 * a device is disconnected
 */
static void my_wiimote_remove(struct hid_device *hdev)
{
	device_remove_file(&hdev->dev, &dev_attr_sysfs_report_mode);
	hid_hw_stop(hdev);
	hid_info(hdev, "Wiimote-Driver - my wiimote driver has been removed\n");
}

/* HID driver declaration - this driver is just an expansion on the normal underlying
 * HID system in linux, and this is where we define to that system what we are looking
 * for and what functions to use for events like probe or remove
 */
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

