#include <ntifs.h>

extern "C" {
	
	// https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/wdm/nf-wdm-iocreatedevice
	NTKERNELAPI NTSTATUS IoCreateDriver(
		PUNICODE_STRING DriverName,
		PDRIVER_INITIALIZE DriverInit
	);

	// https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/ntddk/nf-ntddk-mmcopymemory
	NTKERNELAPI NTSTATUS MmCopyVirtualMemory(
		 PEPROCESS SourceProcess,
		 PVOID SourceAddress,
		PEPROCESS TargetProcess,
		PVOID TargetAddress,
		SIZE_T BufferSize,
		KPROCESSOR_MODE PreviousMode,
		PSIZE_T ReturnSize
	);
}

void debugPrint(PCSTR text) {

	// windbg break and run: ed nt!Kd_IHVDRIVER_MASK 8
	KdPrintEx((DPFLTR_IHVDRIVER_ID, DPFLTR_INFO_LEVEL, "%s\n", text));
}

namespace driver {
	namespace ctlCodes {
		// setup driver
		constexpr ULONG attach = CTL_CODE(FILE_DEVICE_UNKNOWN, 0x696, METHOD_BUFFERED, FILE_SPECIAL_ACCESS);
		// read/write memory
		constexpr ULONG read = CTL_CODE(FILE_DEVICE_UNKNOWN, 0x697, METHOD_BUFFERED, FILE_SPECIAL_ACCESS);
		constexpr ULONG write = CTL_CODE(FILE_DEVICE_UNKNOWN, 0x698, METHOD_BUFFERED, FILE_SPECIAL_ACCESS);
	}

	struct Request {
		HANDLE processID;

		PVOID targetAddress;
		PVOID buffer;

		SIZE_T bufferSize;
		SIZE_T returnSize;
	};


	NTSTATUS create(PDEVICE_OBJECT deviceObject, PIRP irp) {

		UNREFERENCED_PARAMETER(deviceObject);
		
		// irp is the I/O request packet, it contains information about the request
		IoCompleteRequest(irp, IO_NO_INCREMENT);
		
		return irp->IoStatus.Status;
	}

	NTSTATUS close(PDEVICE_OBJECT deviceObject, PIRP irp) {

		UNREFERENCED_PARAMETER(deviceObject);

		// irp is the I/O request packet, it contains information about the request
		IoCompleteRequest(irp, IO_NO_INCREMENT);

		return irp->IoStatus.Status;
	}
	
	// TODO: implement device control
	NTSTATUS deviceControl(PDEVICE_OBJECT deviceObject, PIRP irp) {

		UNREFERENCED_PARAMETER(deviceObject);

		debugPrint("Device control called...\n");

		NTSTATUS status = STATUS_UNSUCCESSFUL;

		// determine the control code that was sent from umode
		PIO_STACK_LOCATION irpStack = IoGetCurrentIrpStackLocation(irp);

		// access the request object sent from umode
		auto request = reinterpret_cast<Request*>(irp->AssociatedIrp.SystemBuffer);

		if (irpStack == nullptr || request == nullptr) {

			IoCompleteRequest(irp, IO_NO_INCREMENT);

			return status;
		}
		
		// targer process we want access to
		static PEPROCESS targetProcess = nullptr;

		const ULONG controlCode = irpStack->Parameters.DeviceIoControl.IoControlCode;

		switch (controlCode) {
		// case 1
		case ctlCodes::attach:
			// just populate targetProcess with the process ID we got from umode
			status = PsLookupProcessByProcessId(request->processID, &targetProcess);

			break;

		// case 2
		case ctlCodes::read:
			if (targetProcess != nullptr) {
				status = MmCopyVirtualMemory(
					PsGetCurrentProcess(),
					request->targetAddress,
					targetProcess,
					request->buffer,
					request->bufferSize,
					KernelMode,
					&request->returnSize
				);
			}

			break;

		// case 3
		case ctlCodes::write:
			if (targetProcess != nullptr) {
				status = MmCopyVirtualMemory(
					PsGetCurrentProcess(),
					request->buffer,
					targetProcess,
					request->targetAddress,
					request->bufferSize,
					KernelMode,
					&request->returnSize
				);
			}

			break;
		
		default:
			break;
		}

		irp->IoStatus.Status = status;
		irp->IoStatus.Information = sizeof(Request);

		IoCompleteRequest(irp, IO_NO_INCREMENT);

		return irp->IoStatus.Status;
	}
}

// our "real" entry point, this is called by IoCreateDriver
NTSTATUS driverEntry(PDRIVER_OBJECT driverObject, PUNICODE_STRING registryPath) {

	// we won't use registrysettings, so we can ignore this parameter
	UNREFERENCED_PARAMETER(registryPath);
	
	UNICODE_STRING deviceName = {};
	RtlInitUnicodeString(&deviceName, L"\\Device\\SMDriver");

	PDEVICE_OBJECT deviceObject = nullptr;

	NTSTATUS status = IoCreateDevice(
		driverObject,
		0,
		&deviceName,
		FILE_DEVICE_UNKNOWN,
		FILE_DEVICE_SECURE_OPEN,
		FALSE,
		&deviceObject
	);

	if (status != STATUS_SUCCESS) {
		debugPrint("Failed to create device...\n");

		return status;
	}

	debugPrint("Device created successfully!\n");

	UNICODE_STRING symlinkName = {};
	RtlInitUnicodeString(&symlinkName, L"\\DosDevices\\SMDriver");

	status = IoCreateSymbolicLink(&symlinkName, &deviceName);

	if (status != STATUS_SUCCESS) {
		debugPrint("Failed to create symbolic link...\n");

		IoDeleteDevice(deviceObject);
		
		return status;
	}

	debugPrint("Symbolic link created successfully!\n");
	
	// allow us to send small amounts of data between umode and kmode
	SetFlag(deviceObject->Flags, DO_BUFFERED_IO);

	// do stuff, like setting up the dispatch routines

	// when events happen, the driver will call functions stored in the major function array
	driverObject->MajorFunction[IRP_MJ_CREATE] = driver::create;
	driverObject->MajorFunction[IRP_MJ_CLOSE] = driver::close;
	driverObject->MajorFunction[IRP_MJ_DEVICE_CONTROL] = driver::deviceControl;

	ClearFlag(deviceObject->Flags, DO_DEVICE_INITIALIZING);

	debugPrint("Driver initialized successfully!\n");

	return STATUS_SUCCESS;
}

// dont give args to DriverEntry as we will manually map this driver using kdmapper, they will be null anyways
NTSTATUS DriverEntry() {

	debugPrint("Hello from kernel mode\n");

	UNICODE_STRING driverName = {};
	RtlInitUnicodeString(&driverName, L"\\Driver\\SMDriver");
	

	return IoCreateDriver(&driverName, &driverEntry);
}