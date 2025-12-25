// int_stack.c — символьный драйвер стека (C)
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/uaccess.h>
#include <linux/slab.h>
#include <linux/mutex.h>
#include <linux/ioctl.h>
#include <linux/miscdevice.h>
#include <linux/device.h>
#include <linux/usb.h>
#include <linux/errno.h>

#define DEVICE_NAME "int_stack"
#define CLASS_NAME "stack"
#define USB_DEVICE_NAME "int_stack_usb"

// IOCTL команды
#define INT_STACK_IOC_MAGIC 's'
#define INT_STACK_SET_SIZE _IOW(INT_STACK_IOC_MAGIC, 1, int)

#define USB_VENDOR_ID  0x18d1
#define USB_PRODUCT_ID 0x4ee8

struct int_stack {
    int *buf;
    size_t max_size;
    size_t top;
    struct mutex lock;
    struct usb_device *usb_dev;
    atomic_t key_present;
};

static struct int_stack stack = {0};
static struct miscdevice int_stack_miscdev;

static int int_stack_open(struct inode *inode, struct file *file) {
    if (!atomic_read(&stack.key_present)) {
        pr_err("int_stack: USB key not presented\n");
        return -ENODEV;
    }
    pr_info("int_stack: open(USB key presented)\n");
    return 0;
}

static int int_stack_release(struct inode *inode, struct file *file) {
    pr_info("int_stack: release\n");
    return 0;
}

static ssize_t int_stack_read(struct file *file, char __user *user_buf,
                              size_t count, loff_t *ppos) {
    char tmp[32];
    ssize_t len;
    
    if (!atomic_read(&stack.key_present))
        return -ENODEV;

    mutex_lock(&stack.lock);
    
    if (stack.top == 0) {
        mutex_unlock(&stack.lock);
        return 0;
    }
    
    stack.top--;
    int val = stack.buf[stack.top];
    
    len = snprintf(tmp, sizeof(tmp), "%d\n", val);
    
    mutex_unlock(&stack.lock);
    
    if (len > count)
        len = count;
    
    if (copy_to_user(user_buf, tmp, len))
        return -EFAULT;
    
    return len;
}

static ssize_t int_stack_write(struct file *file, const char __user *user_buf,
                               size_t count, loff_t *ppos) {
    char tmp[32];
    int val;
    
    if (!atomic_read(&stack.key_present))
        return -ENODEV;

    if (count >= sizeof(tmp))
        return -EINVAL;
    
    // Читаем строку из userspace
    if (copy_from_user(tmp, user_buf, count))
        return -EFAULT;
    tmp[count] = '\0';
    
    if (sscanf(tmp, "%d", &val) != 1)
        return -EINVAL;
    
    mutex_lock(&stack.lock);
    
    if (stack.top == stack.max_size) {
        mutex_unlock(&stack.lock);
        return -ERANGE;
    }
    
    stack.buf[stack.top++] = val;
    mutex_unlock(&stack.lock);
    
    return count;
}

static long int_stack_ioctl(struct file *file, unsigned int cmd, 
                           unsigned long arg) {
    int new_size;
    
    if (!atomic_read(&stack.key_present))
        return -ENODEV;

    if (_IOC_TYPE(cmd) != INT_STACK_IOC_MAGIC)
        return -ENOTTY;
    
    if (_IOC_NR(cmd) > 1)
        return -ENOTTY;
    
    switch (cmd) {
    case INT_STACK_SET_SIZE:
        if (copy_from_user(&new_size, (int __user *)arg, sizeof(new_size)))
            return -EFAULT;
        
        mutex_lock(&stack.lock);
        
        if (new_size <= 0) {
            mutex_unlock(&stack.lock);
            return -EINVAL;
        }
        
        if (new_size == stack.max_size) {
            mutex_unlock(&stack.lock);
            return 0;
        }
        
        if (new_size > stack.max_size) {
            // Увеличиваем: новый kmalloc большего размера
            int *new_buf = kmalloc(new_size * sizeof(int), GFP_KERNEL);
            if (!new_buf) {
                mutex_unlock(&stack.lock);
                return -ENOMEM;
            }
            memcpy(new_buf, stack.buf, stack.top * sizeof(int));
            kfree(stack.buf);
            stack.buf = new_buf;
            stack.max_size = new_size;
        } else {
            pr_info("int_stack: auto-pop %zu → %d\n", stack.top, new_size);
            stack.max_size = new_size;
        }

        if (stack.top > new_size) {
            stack.top = new_size;
        }
        
        mutex_unlock(&stack.lock);
        return 0;
        
    default:
        return -ENOTTY;
    }
}

static const struct file_operations fops = {
    .owner = THIS_MODULE,
    .open = int_stack_open,
    .release = int_stack_release,
    .read = int_stack_read,
    .write = int_stack_write,
    .unlocked_ioctl = int_stack_ioctl,
};

static struct miscdevice int_stack_miscdev = {
    .minor = MISC_DYNAMIC_MINOR,
    .name = DEVICE_NAME,
    .fops = &fops,
    .mode = 0666,
};

static int pen_probe(struct usb_interface *interface, const struct usb_device_id *id) {
    struct usb_device *udev = interface_to_usbdev(interface);
    int ret;

    if (interface->cur_altsetting->desc.bInterfaceNumber != 0) {
        pr_info("int_stack_dev: skipping interface %d\n", 
                interface->cur_altsetting->desc.bInterfaceNumber);
        return 0;
    }

    pr_info("int_stack_dev: Pen drive (%04X:%04X) plugged\n", id->idVendor, id->idProduct);

    if(id->idVendor == USB_VENDOR_ID && id->idProduct == USB_PRODUCT_ID) {
        atomic_set(&stack.key_present, 1);
        stack.usb_dev = udev;

        ret = misc_register(&int_stack_miscdev);
        if (ret) {
            pr_err("int_stack_dev: failed to register miscdev\n");
            atomic_set(&stack.key_present, 0);
            return ret;
        }
        pr_info("int_stack_dev: registered /dev/int_stack\n");
    }

    return 0;
}

static void pen_disconnect(struct usb_interface *interface) {

    if (interface->cur_altsetting->desc.bInterfaceNumber != 0)
        return;

    pr_info("int_stack_dev: pen drive unplugged\n");
    atomic_set(&stack.key_present, 0);
    misc_deregister(&int_stack_miscdev);
    stack.usb_dev = NULL;
    pr_info("int_stack_dev: /dev/int_stack removed\n");
}

static struct usb_device_id pen_key_table[] = {
    { USB_DEVICE(USB_VENDOR_ID, USB_PRODUCT_ID) },
    { }
};
MODULE_DEVICE_TABLE(usb, pen_key_table);

static struct usb_driver pen_driver =
{
    .name = USB_DEVICE_NAME,
    .id_table = pen_key_table,
    .probe = pen_probe,
    .disconnect = pen_disconnect,
};


static int __init int_stack_init(void) {
    int ret;
    
    pr_info("int_stack_dev: loading\n");
    
    stack.max_size = 16;
    stack.buf = kmalloc(stack.max_size * sizeof(int), GFP_KERNEL);
    if (!stack.buf)
        return -ENOMEM;
    stack.top = 0;
    mutex_init(&stack.lock);
    atomic_set(&stack.key_present, 0);

    ret = usb_register(&pen_driver);

    if (ret) {
        pr_err("int_stack_dev: failed to register USB driver\n");
        mutex_destroy(&stack.lock);
        kfree(stack.buf);
        return ret;
    }
    
    pr_info("int_stack_dev: USB driver registered. Waiting for key (%04X:%04X)\n", USB_VENDOR_ID, USB_PRODUCT_ID);
    return 0;
}

static void __exit int_stack_exit(void) {
    pr_info("int_stack_dev: unloading USB driver...\n");
    
    usb_deregister(&pen_driver);

    mutex_destroy(&stack.lock);
    kfree(stack.buf); 
    
    pr_info("int_stack_dev: unloaded USB driver\n");
}

module_init(int_stack_init);
module_exit(int_stack_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Ildar Islamov <ildar.islamov.job@gmail.com>");
MODULE_DESCRIPTION("Integer stack chardev");
