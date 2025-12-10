=====================================
H15 Expanding SDCard Available Space
=====================================

This quick guide shows how to take advantages of creating a new partition for the available SDCard space since some of Hailo-15 official images root fs is only around 2.4GB.

.. note::
   The console output shown below might differ slightly depending on the case and on a different device block but the overall steps should be the same.

.. contents:: Quick Steps
   :depth: 1
   :local:

1. lsblk
========

The following will appear (see below), which indicate the disk is mmcblk1 and its total size is 58GB while it is only taking 2.5GB (mmcblk1p2).

.. code-block:: console

   root@hailo15:~# lsblk
   NAME        MAJ:MIN RM  SIZE RO TYPE MOUNTPOINTS
   mtdblock0    31:0   0   32M  0 disk
   mmcblk1     179:0   0   58G  0 disk
   ├─mmcblk1p1 179:1   0   64M  0 part /boot
   └─mmcblk1p2 179:2   0  2.5G  0 part /

2. fdisk /dev/mmcblk1
=====================

After executing the command, start the partitioning by typing following step below:

* **n** → new partition
* **p** → primary
* **3** → partition number (since p1 and p2 already exist)
* **Press Enter** to accept the default start sector (should be 5405574 or close — must be after ``/dev/mmcblk1p2``'s end).
* **Press Enter** to accept the default end sector (will use all remaining space).
* **w** → write changes and exit.

3. reboot
=========

After reboot check if it shows ``/dev/mmcblk1p3`` by executing ``fdisk -l /dev/mmcblk1``

.. code-block:: console

   root@hailo15:~# fdisk -l /dev/mmcblk1
   Disk /dev/mmcblk1: 57.96 GiB, 62239277056 bytes, 121561088 sectors
   Units: sectors of 1 * 512 = 512 bytes
   Sector size (logical/physical): 512 bytes / 512 bytes
   I/O size (minimum/optimal): 512 bytes / 512 bytes
   Disklabel type: dos
   Disk identifier: 0xcc2233d9

   Device       Boot   Start       End   Sectors  Size Id Type
   /dev/mmcblk1p1 *        8    131079    131072   64M  c W95 FAT32 (LBA)
   /dev/mmcblk1p2      131080   5405573   5274494  2.5G 83 Linux
   /dev/mmcblk1p3     5406720 121561087 116154368 55.4G 83 Linux

4. mkfs.ext4 /dev/mmcblk1p3
===========================

Start formatting the partition; this process may take a few minutes.

5. mkdir /data
==============

Create the directory where the new partition will be mounted. It does not have to be ``/data``; any directory under root can be used.

6. mount /dev/mmcblk1p3 /data
=============================

The new partition is now mounted on the new directory created in the step above.

7. df -h /data
==============

Verify that output similar to the following is displayed (actual size depends on the SD card):

.. code-block:: console

   root@hailo15:~# df -h /data
   Filesystem      Size  Used Avail Use% Mounted on
   /dev/mmcblk1p3   55G   24K   52G   1% /data

8. echo '/dev/mmcblk1p3 /data ext4 defaults 0 2' >> /etc/fstab
================================================================

It is highly recommended to make it consistent so that it won’t be necessary to mount after every reboot.
