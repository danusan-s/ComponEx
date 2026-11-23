#include "componeng/renderer/draw/instance_buffer_cache.hpp"

namespace componeng::renderer {

std::size_t InstanceBufferCache::upload(api::IRenderDevice &device,
                                        const RenderQueue &queue) {
  std::size_t uploaded = 0;
  for (std::size_t i = 0; i < queue.size(); ++i) {
    const RenderBatch &batch = queue.sorted(i);
    if (batch.procedural) {
      continue;
    }
    std::unique_ptr<api::IBuffer> &buf =
        m_buffers[DrawKey{batch.meshID, batch.materialID}];
    if (!buf) {
      buf = device.createBuffer();
    }
    const std::size_t requiredBytes = batch.instanceData.size() * sizeof(float);
    uploaded += requiredBytes;
    if (requiredBytes > buf->size()) {
      buf->setData(batch.instanceData.data(), requiredBytes,
                   api::IBuffer::Usage::Dynamic);
    } else if (requiredBytes > 0) {
      buf->setSubData(0, batch.instanceData.data(), requiredBytes);
    }
  }
  return uploaded;
}

api::IBuffer &InstanceBufferCache::bufferFor(const RenderBatch &batch) const {
  return *m_buffers.at(DrawKey{batch.meshID, batch.materialID});
}

} // namespace componeng::renderer
