#include "serializer.h"
#include "isolate/allocator.h"

using namespace v8;
namespace ivm {

ExternalCopySerialized::ExternalCopySerialized(Local<Object> value, ArrayRange transfer_list) {

	// Initialize serializer and delegate
	Isolate* isolate = Isolate::GetCurrent();
	Local<Context> context = isolate->GetCurrentContext();
	std::deque<std::unique_ptr<Transferable>> transferables;
	detail::ExternalCopySerializerDelegate delegate{transferables, wasm_modules};
	ValueSerializer serializer{isolate, &delegate};
	delegate.SetSerializer(&serializer);

	// Transfer ArrayBuffers
	for (auto handle : transfer_list) {
		if (handle->IsArrayBuffer()) {
			auto array_buffer = handle.As<ArrayBuffer>();
			serializer.TransferArrayBuffer(transferables.size(), array_buffer);
			array_buffers.emplace_back(ExternalCopyArrayBuffer::Transfer(array_buffer));
		} else {
			throw RuntimeTypeError("Non-ArrayBuffer passed in `transferList`");
		}
	}

	// Serialize object and save
	serializer.WriteHeader();
	Unmaybe(serializer.WriteValue(context, value));
	auto serialized_data = serializer.Release();
	buffer = {serialized_data.first, std::free};
	size = serialized_data.second;
}

auto ExternalCopySerialized::CopyInto(bool transfer_in) -> Local<Value> {

	// Initialize deserializer and delegate
	Isolate* isolate = Isolate::GetCurrent();
	Local<Context> context = isolate->GetCurrentContext();
	auto allocator = dynamic_cast<LimitedAllocator*>(IsolateEnvironment::GetCurrent()->GetAllocator());
	int failures = allocator == nullptr ? 0 : allocator->GetFailureCount();
	detail::ExternalCopyDeserializerDelegate delegate{transferables, wasm_modules};
	ValueDeserializer deserializer{isolate, buffer.get(), size, &delegate};
	delegate.SetDeserializer(&deserializer);

	// Transfer ArrayBuffers
	for (unsigned ii = 0; ii < array_buffers.size(); ++ii) {
		deserializer.TransferArrayBuffer(ii, array_buffers[ii]->CopyIntoCheckHeap(transfer_in).As<ArrayBuffer>());
	}

	// Deserialize object
	Unmaybe(deserializer.ReadHeader(context));
	Local<Value> value;
	if (deserializer.ReadValue(context).ToLocal(&value)) {
		return value;
	} else {
		// ValueDeserializer throws an unhelpful message when it fails to allocate an ArrayBuffer, so
		// detect that case here and throw an appropriate message.
		if (allocator != nullptr && allocator->GetFailureCount() != failures) {
			throw RuntimeRangeError("Array buffer allocation failed");
		} else {
			throw RuntimeError();
		}
	}
}

} // namespace ivm
