/* Functions local to drivers/usb/core/ */

extern void usb_create_sysfs_dev_files (struct usb_device *dev);
extern void usb_remove_sysfs_dev_files (struct usb_device *dev);
extern void usb_create_sysfs_intf_files (struct usb_interface *intf);
extern void usb_remove_sysfs_intf_files (struct usb_interface *intf);

extern void usb_disable_endpoint (struct usb_device *dev, unsigned int epaddr);
extern void usb_disable_interface (struct usb_device *dev,
		struct usb_interface *intf);
extern void usb_release_interface_cache(struct kref *ref);
extern void usb_disable_device (struct usb_device *dev, int skip_ep0);

extern int usb_get_device_descriptor(struct usb_device *dev,
		unsigned int size);
extern int usb_set_configuration(struct usb_device *dev, int configuration);

extern void usb_lock_all_devices(void);
extern void usb_unlock_all_devices(void);

extern void usb_kick_khubd(struct usb_device *dev);
extern void usb_resume_root_hub(struct usb_device *dev);

/* for labeling diagnostics */
extern const char *usbcore_name;

/* usbfs stuff */
extern struct usb_driver usbfs_driver;
extern struct file_operations usbfs_devices_fops;
extern struct file_operations usbfs_device_file_operations;
extern void usbfs_conn_disc_event(void);

struct dev_state {
	struct list_head list;      /* state list */
	struct usb_device *dev;
	struct file *file;
	spinlock_t lock;            /* protects the async urb lists */
	struct list_head async_pending;
	struct list_head async_completed;
	wait_queue_head_t wait;     /* wake up if a request completed */
	unsigned int discsignr;
	struct task_struct *disctask;
	void __user *disccontext;
	unsigned long ifclaimed;
};

