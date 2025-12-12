#include "imc/imc_unit.hh"

#include <cstring>

#include "base/trace.hh"
#include "debug/IMCMAC.hh"
#include "mem/packet.hh"
#include "mem/packet_access.hh"

namespace gem5
{

    // =========================================================================
    // 1. CONSTRUCTOR & INITIALIZATION
    // =========================================================================

    IMCMAC::IMCMAC(const Params &p)
        : DmaDevice(p),
          num_physical_rows(p.num_physical_rows),
          num_physical_cols(p.num_physical_cols),
          rows_per_compute_cycle(p.rows_per_compute_cycle),
          max_input_precision(p.max_input_precision),
          max_weight_precision(p.max_weight_precision),
          max_output_precision(p.max_output_precision),
          num_entries(p.num_entries),
          input_bytes_per_element(0),
          weight_bytes_per_element(0),
          output_bytes_per_element(4),
          latency_sram_read(p.latency_sram_read),
          latency_cell_logic(p.latency_cell_logic),
          latency_adder_tree(p.latency_adder_tree),
          enable_pipelining(p.enable_pipelining),
          pioAddr(p.pio_addr), pioSize(0x100), pioDelay(p.pio_latency),
          dmaDoneEvent([this]
                       { dmaComplete(); }, name()),
          computeDoneEvent([this]
                           { processComputeEvent(); }, name()),
          stats(this)
    {
        // --- 1. Determine Container Sizes (Padding Logic) ---
        if (max_input_precision <= 8)
            input_bytes_per_element = 1;
        else if (max_input_precision <= 16)
            input_bytes_per_element = 2;
        else
            input_bytes_per_element = 4;

        if (max_weight_precision <= 8)
            weight_bytes_per_element = 1;
        else if (max_weight_precision <= 16)
            weight_bytes_per_element = 2;
        else
            weight_bytes_per_element = 4;

        // --- 2. Allocation (Bytes) ---
        input_buffer.resize(
            num_entries * num_physical_rows * input_bytes_per_element, 0);

        weight_array.resize(
            num_physical_cols * num_physical_rows * weight_bytes_per_element, 0);

        output_buffer.resize(
            num_entries * num_physical_cols * output_bytes_per_element, 0);

        // --- 3. Registers ---
        regs.status = 0;
        regs.cmd = 0;
        regs.input_len = 0;
        regs.num_vectors = 0;
        regs.prec_input = max_input_precision;
        regs.prec_weight = max_weight_precision;
        regs.dma_addr = 0;
        regs.dma_len = 0;
        regs.dma_type = 0;
        regs.data_type = 0x3; // Default: Signed Inputs, Signed Weights
    }

    // =========================================================================
    // 2. HELPER FUNCTIONS (Safe Access)
    // =========================================================================

    int32_t
    IMCMAC::readInput(unsigned index)
    {
        unsigned offset = index * input_bytes_per_element;
        bool is_signed = (regs.data_type & 0x1);

        if (input_bytes_per_element == 1)
        {
            uint8_t raw = input_buffer[offset];
            return is_signed ? (int8_t)raw : raw;
        }
        else if (input_bytes_per_element == 2)
        {
            uint16_t raw;
            std::memcpy(&raw, &input_buffer[offset], 2);
            return is_signed ? (int16_t)raw : raw;
        }
        else
        {
            uint32_t raw;
            std::memcpy(&raw, &input_buffer[offset], 4);
            return is_signed ? (int32_t)raw : raw;
        }
    }

    int32_t
    IMCMAC::readWeight(unsigned index)
    {
        unsigned offset = index * weight_bytes_per_element;
        bool is_signed = (regs.data_type & 0x2);

        if (weight_bytes_per_element == 1)
        {
            uint8_t raw = weight_array[offset];
            return is_signed ? (int8_t)raw : raw;
        }
        else if (weight_bytes_per_element == 2)
        {
            uint16_t raw;
            std::memcpy(&raw, &weight_array[offset], 2);
            return is_signed ? (int16_t)raw : raw;
        }
        else
        {
            uint32_t raw;
            std::memcpy(&raw, &weight_array[offset], 4);
            return is_signed ? (int32_t)raw : raw;
        }
    }

    // =========================================================================
    // 3. PIO INTERFACE
    // =========================================================================

    AddrRangeList
    IMCMAC::getAddrRanges() const
    {
        return AddrRangeList({AddrRange(pioAddr, pioAddr + pioSize)});
    }

    Tick
    IMCMAC::read(PacketPtr pkt)
    {
        Addr offset = pkt->getAddr() - pioAddr;
        uint32_t data = 0;
        switch (offset)
        {
        case REG_STATUS:
            data = regs.status;
            break;
        case REG_CMD:
            data = regs.cmd;
            break;
        case REG_INPUT_LEN:
            data = regs.input_len;
            break;
        case REG_NUM_VECTORS:
            data = regs.num_vectors;
            break;
        case REG_PREC_IN:
            data = regs.prec_input;
            break;
        case REG_PREC_W:
            data = regs.prec_weight;
            break;
        case REG_DMA_ADDR_L:
            data = (uint32_t)(regs.dma_addr & 0xFFFFFFFF);
            break;
        case REG_DMA_ADDR_H:
            data = (uint32_t)(regs.dma_addr >> 32);
            break;
        case REG_DMA_LEN:
            data = regs.dma_len;
            break;
        case REG_DMA_TYPE:
            data = regs.dma_type;
            break;
        case REG_DATA_TYPE:
            data = regs.data_type;
            break;
        default:
            warn("IMCMAC: Invalid Read Offset 0x%x", offset);
            break;
        }
        pkt->setLE(data);
        pkt->makeResponse();
        return pioDelay;
    }

    Tick
    IMCMAC::write(PacketPtr pkt)
    {
        Addr offset = pkt->getAddr() - pioAddr;
        uint32_t data = pkt->getLE<uint32_t>();
        switch (offset)
        {
        case REG_CMD:
            regs.cmd = data;
            if (data == 1 && regs.status == 0)
            {
                regs.status = 1;
                calculateLatency();
            }
            else if (data == 2 && regs.status == 0)
            {
                regs.status = 1;
                startDma();
            }
            break;
        case REG_INPUT_LEN:
            regs.input_len = data;
            break;
        case REG_NUM_VECTORS:
            regs.num_vectors = data;
            break;
        case REG_PREC_IN:
            regs.prec_input = data;
            break;
        case REG_PREC_W:
            regs.prec_weight = data;
            break;
        case REG_DMA_ADDR_L:
            regs.dma_addr = (regs.dma_addr & 0xFFFFFFFF00000000) | data;
            break;
        case REG_DMA_ADDR_H:
            regs.dma_addr =
                (regs.dma_addr & 0x00000000FFFFFFFF) | ((uint64_t)data << 32);
            break;
        case REG_DMA_LEN:
            regs.dma_len = data;
            break;
        case REG_DMA_TYPE:
            regs.dma_type = data;
            break;
        case REG_DATA_TYPE:
            regs.data_type = data;
            break;
        default:
            break;
        }
        pkt->makeResponse();
        return pioDelay;
    }

    // =========================================================================
    // 4. CORE LOGIC
    // =========================================================================

    void
    IMCMAC::calculateLatency()
    {
        unsigned num_passes =
            (regs.input_len + rows_per_compute_cycle - 1) / rows_per_compute_cycle;

        Tick fetch_latency = latency_sram_read;
        Tick process_latency = num_passes * regs.prec_input * latency_cell_logic;
        Tick total_latency = (fetch_latency + process_latency) * regs.num_vectors;

        current_txn_latency = total_latency;

        stats.numComputes++;
        stats.totalCycles += (total_latency / clockPeriod());
        stats.numSramFetches += regs.num_vectors;
        stats.totalMACs += (regs.num_vectors * regs.input_len * num_physical_cols);
        schedule(computeDoneEvent, curTick() + total_latency);
    }

    void
    IMCMAC::processComputeEvent()
    {
        performCompute();
        regs.status = 0;
        DPRINTF(IMCMAC, "Compute Tile Finished.\n");
    }

    void
    IMCMAC::performCompute()
    {
        uint32_t batch_size = regs.num_vectors;
        uint32_t mask_limit = regs.input_len;

        int32_t *out_ptr = (int32_t *)output_buffer.data();

        // Loop over each row of the input buffer (Batch)
        for (uint32_t vec_idx = 0; vec_idx < batch_size; ++vec_idx)
        {

            // Index for the start of the input vector
            unsigned input_vec_start_idx = vec_idx * num_physical_rows;

            // Index for the start of the output vector
            unsigned output_vec_start_idx = vec_idx * num_physical_cols;

            // Initialize Output Row to Zero
            for (uint32_t c = 0; c < num_physical_cols; ++c)
            {
                out_ptr[output_vec_start_idx + c] = 0;
            }

            // --- ROW LOOP (Input Dimension) ---
            // This iterates through the inputs connected to the wordlines.
            for (uint32_t row = 0; row < mask_limit; ++row)
            {

                // Safety: Physical Constraint
                if (row >= num_physical_rows)
                    break;

                // 1. Get Input (Broadcasting behavior)
                // We read this once and reuse it for all columns (Wordline logic).
                int32_t input_val = readInput(input_vec_start_idx + row);

                // 2. Multiply with all Weights in this row
                // Each input connects to 'num_physical_cols' weights.
                for (uint32_t col = 0; col < num_physical_cols; ++col)
                {
                    // Weights are stored sequentially by row:
                    // Weight_Index = (row * num_cols) + col
                    int32_t weight_val =
                        readWeight((row * num_physical_cols) + col);

                    // Accumulate partial sum
                    out_ptr[output_vec_start_idx + col] +=
                        (input_val * weight_val);
                }
            }
        }
        stats.numSramWrites += (batch_size * num_physical_cols);
    }

    // =========================================================================
    // 5. DMA ENGINE
    // =========================================================================

    void
    IMCMAC::startDma()
    {
        uint8_t *data_ptr = nullptr;
        bool is_read = true;
        switch (regs.dma_type)
        {
        case 0:
            data_ptr = input_buffer.data();
            is_read = false;
            break;
        case 1:
            data_ptr = weight_array.data();
            is_read = false;
            break;
        case 2:
            data_ptr = output_buffer.data();
            is_read = true;
            break;
        case 3:
            data_ptr = output_buffer.data();
            is_read = false;
            break;
        default:
            warn("IMCMAC: Invalid DMA Type");
            regs.status = 0;
            return;
        }

        dmaPort.dmaAction(is_read ? MemCmd::ReadReq : MemCmd::WriteReq,
                          regs.dma_addr, regs.dma_len, &dmaDoneEvent, data_ptr, 0);

        if (is_read)
            stats.dmaBytesWritten += regs.dma_len;
        else
            stats.dmaBytesRead += regs.dma_len;
    }

    void
    IMCMAC::dmaComplete()
    {
        regs.status = 0;
        DPRINTF(IMCMAC, "DMA Transaction Complete\n");
    }

    IMCMAC::IMCMACStats::IMCMACStats(statistics::Group *parent)
        : statistics::Group(parent),
          ADD_STAT(numComputes, "Tiles computed"),
          ADD_STAT(totalCycles, "Busy cycles"),
          ADD_STAT(numSramFetches, "Wide Fetches"),
          ADD_STAT(numSramWrites, "Local SRAM writes"),
          ADD_STAT(totalMACs, "Masked MAC ops"),
          ADD_STAT(dmaBytesRead, "DRAM reads"),
          ADD_STAT(dmaBytesWritten, "DRAM writes")
    {
    }

} // namespace gem5