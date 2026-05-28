# mp3.make - MP3 makefile
# 

PREFIX=riscv64-unknown-elf-
QEMU=qemu-system-riscv64

CC=$(PREFIX)gcc
AS=$(PREFIX)as
LD=$(PREFIX)ld
OBJCOPY=$(PREFIX)objcopy
OBJDUMP=$(PREFIX)objdump
UNIFDEF=unifdef

TARGET=qvirt

STUDENT_CP1=0

OBJS = \
	board/$(TARGET)/init.o \
	dev/rtc.o \
	dev/uart.o \
	dev/vioblk.o \
	dev/virtio.o \
	dev/viorng.o \
	fs/ngfs.o \
	fs/tarfs.o \
	cache.o \
	console.o \
	device.o \
	elf.o\
	error.o \
	excp.o \
	filesys.o \
	heap.o \
	intr.o \
	io.o \
	misc.o \
	plic.o \
	sbi.o \
	start.o \
	string.o \
	thrasm.o \
	thread.o \
	timer.o \
	trap.o



CFLAGS = -Wall -Werror=implicit-function-declaration -Wno-unused-function
CFLAGS += -fno-omit-frame-pointer -ggdb3 -gdwarf-2
CFLAGS += -mcmodel=medany -fno-pie -no-pie -march=rv64imazicsr -mabi=lp64
CFLAGS += -fno-common -nostdlib -mno-relax -ffreestanding 
CFLAGS += -fno-asynchronous-unwind-tables -mno-riscv-attribute
CFLAGS += -I.
CFLAGS += -Iboard/$(TARGET)

# CFLAGS += -DDEBUG -DTRACE # Everything!

# CFLAGS += -DMEMORY_DEBUG -DMEMORY_TRACE
# CFLAGS += -DHEAP_DEBUG -DHEAP_TRACE

# CFLAGS += -DTHREAD_DEBUG -DTHREAD_TRACE
# CFLAGS += -DLOCK_DEBUG -DLOCK_TRACE
# CFLAGS += -DPROCESS_DEBUG -DPROCESS_TRACE
# CFLAGS += -DSYSCALL_DEBUG -DSYSCALL_TRACE
# CFLAGS += -DELF_DEBUG -DELF_TRACE

# CFLAGS += -DIO_DEBUG -DIO_TRACE
# CFLAGS += -DVIRTIO_DEBUG -DVIRTIO_TRACE
# CFLAGS += -DTIMER_DEBUG -DTIMER_TRACE
# CFLAGS += -DVIOGPU_DEBUG -DVIOGPU_TRACE
# CFLAGS += -DVIOBLK_DEBUG -DVIOBLK_TRACE

# CFLAGS += -DCACHE_DEBUG -DCACHE_TRACE
# CFLAGS += -DKTFS_DEBUG -DKTFS_TRACE
# CFLAGS += -DNGFS_DEBUG -DNGFS_TRACE

# CFLAGS += -DMAIN_DEBUG -DMAIN_TRACE


ASFLAGS = -march=rv64imazicsr

LDFLAGS = -melf64lriscv -T board/$(TARGET)/kernel.ld

QEMUOPTS = -global virtio-mmio.force-legacy=false
QEMUOPTS += -machine virt -nographic
QEMUOPTS += -object rng-random,filename=/dev/urandom,id=rng0
QEMUOPTS += -device virtio-rng-device,rng=rng0
QEMUOPTS += -bios bios/osbi181q.bin
QEMUOPTS += -serial mon:stdio
QEMUOPTS += -serial pty
QEMUOPTS += -serial pty
QEMUOPTS += -device virtio-blk-device,drive=blk0
QEMUOPTS += -drive file=fs/ngfs.raw,id=blk0,if=none,format=raw,readonly=false
QEMUOPTS += -device virtio-blk-device,drive=blk1
QEMUOPTS += -drive file=fs/tarfs.tar,id=blk1,if=none,format=raw,readonly=false


ifeq ($(STUDENT_CP1), 1)
	CFLAGS += -DSTUDENT_CP1
	ASFLAGS += -defsym STUDENT_CP1=1
else
	OBJS += \
		memory.o \
		process.o \
		syscall.o
endif

all: kernel.elf

clean:
	rm -rf board/*/*.o dev/*.o fs/*.o *.o *.elf

kernel.elf: $(OBJS) blob.o main.o
	$(LD) $(LDFLAGS) -o $@ $^

run: kernel.elf
	$(QEMU) $(QEMUOPTS) -m 16M -kernel $<

debug: kernel.elf
	$(QEMU) $(QEMUOPTS) -m 16M -kernel $< -S -s

BLOB_OBJCOPY_FLAGS = \
	--add-section .data.blob=blob.raw \
	--set-section-flags .data.blob=alloc,contents,load,data

blob.o:
	echo .end | $(AS) $(ASFLAGS) -o blob.o
	[ ! -f blob.raw ] || $(OBJCOPY) $(BLOB_OBJCOPY_FLAGS) $@

%.o: %.c
	$(CC) -c -o $@ $(CFLAGS) $<
