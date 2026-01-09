#include "gpu_imc/digital_imc.hh"
#include "base/trace.hh"
#include "debug/DigitalIMC.hh"
#include "mem/packet_access.hh"
#include <cmath>
#include <algorithm>

namespace gem5
{

    // Constructor for the Stats Group
    DigitalIMC::ImcStats::ImcStats(statistics::Group *parent)
        : statistics::Group(parent),
          ADD_STAT(numMacOps, "Total MAC operations performed"),
          ADD_STAT(numTotalBitOps, "Total Bit-Level operations (Weight bits * Input bits)"),
          ADD_STAT(numSramReads, "Total reads from Internal SRAM"),
          ADD_STAT(numSramWrites, "Total writes to Internal SRAM"),
          ADD_STAT(totalDmaBytesRead, "Total bytes read from DRAM via DMA"),
          ADD_STAT(totalDmaBytesWritten, "Total bytes written to DRAM via DMA")
    {
    }

    DigitalIMC::DigitalIMC(const Params &p)
        : DmaDevice(p),
          // --- 1. Stats come FIRST (Matches Header Order) ---
          stats(this),
          // --- 2. Parameters ---
          pioAddr(p.pio_addr),
          pioSize(0x1000),
          pioDelay(p.pio_latency),
          numRows(p.num_rows),
          numCols(p.num_cols),
          numBanks(p.num_banks),
          maxPrecW(p.max_precision_w),
          maxPrecI(p.max_precision_i),
          // --- 3. Events come LAST (Matches Header Order) ---
          computeDoneEvent([this]
                           { computeDone(); }, name()),
          dmaDoneEvent([this]
                       { dmaDone(); }, name())
    {
        // 1. Initialize Registers
        regs.cmd = 0;
        regs.status = 0;
        regs.prec_w = maxPrecW; // Default to max
        regs.prec_i = maxPrecI; // Default to max
        regs.out_addr = 0;
        regs.sign = 0;           // Default to unsigned
        current_sram_offset = 0; // Initialize internal pointer

        // 2. Configure "Hard" Core (Workspace)
        // Weight Memory limits based on geometry
        uint64_t total_weight_bits = (uint64_t)numBanks * numRows * numCols * maxPrecW;
        uint64_t total_weight_bytes = (total_weight_bits + 7) / 8;
        weight_memory.resize(total_weight_bytes, 0);

        // ACCUMULATORS: Sized by Column Count.
        // We use uint64_t for fast simulation speed.
        // Theoretical Max Width = PrecI + PrecW + log2(Rows) ~ 21-25 bits.
        int acc_bits_needed = maxPrecI + maxPrecW + std::ceil(std::log2(numRows));
        if (acc_bits_needed > 64)
        {
            panic("DigitalIMC: Configured architecture requires %d bits, but simulator uses 64-bit accumulators!",
                  acc_bits_needed);
        }
        internal_accumulators.resize(numBanks * numCols, 0);

        // 3. Configure "Soft" Shell (Warehouse)
        // Sized by User Parameters (e.g., 4kB, 256kB)
        input_sram.resize(p.input_buffer_size, 0);
        output_sram.resize(p.output_buffer_size, 0);

        DPRINTF(DigitalIMC, "--- Digital IMC Architecture Init ---\n");
        DPRINTF(DigitalIMC, "  Geometry: %d Rows, %d Cols, %d Banks\n", numRows, numCols, numBanks);
        DPRINTF(DigitalIMC, "  Weight Storage: %lu bytes (Packed)\n", total_weight_bytes);
        DPRINTF(DigitalIMC, "  Accumulator Width: %d bits (Simulated as 64-bit)\n", acc_bits_needed);
        DPRINTF(DigitalIMC, "  Input SRAM: %lu bytes\n", input_sram.size());
        DPRINTF(DigitalIMC, "  Output SRAM: %lu bytes\n", output_sram.size());
    }

    AddrRangeList DigitalIMC::getAddrRanges() const
    {
        return {RangeSize(pioAddr, pioSize)};
    }

    Tick DigitalIMC::read(PacketPtr pkt)
    {
        Addr offset = pkt->getAddr() - pioAddr;
        unsigned size = pkt->getSize();
        uint64_t data = 0;

        // Read Logic
        switch (offset)
        {
        case REG_CMD:
            data = regs.cmd;
            break;
        case REG_STATUS:
            data = regs.status;
            break;
        case REG_PREC_W:
            data = regs.prec_w;
            break;
        case REG_PREC_I:
            data = regs.prec_i;
            break;
        case REG_W_ADDR:
            data = regs.w_addr;
            break;
        case REG_W_LEN:
            data = regs.w_len;
            break;
        case REG_I_ADDR:
            data = regs.i_addr;
            break;
        case REG_I_LEN:
            data = regs.i_len;
            break;
        case REG_OUT_ADDR:
            data = regs.out_addr;
            break;
        case REG_SIGN:
            data = regs.sign;
            break;
        default:
            data = 0;
            break;
        }

        if (size == 4)
            pkt->setLE<uint32_t>((uint32_t)data);
        else if (size == 8)
            pkt->setLE<uint64_t>(data);
        else
            panic("DigitalIMC: Invalid read size %d", size);

        pkt->makeResponse();
        return pioDelay;
    }

    Tick DigitalIMC::write(PacketPtr pkt)
    {
        Addr offset = pkt->getAddr() - pioAddr;
        unsigned size = pkt->getSize();

        // Ignore OS Cleanups
        if (size == 64)
        {
            pkt->makeResponse();
            return pioDelay;
        }

        uint64_t data = 0;
        if (size == 4)
            data = pkt->getLE<uint32_t>();
        else if (size == 8)
            data = pkt->getLE<uint64_t>();
        else
            panic("DigitalIMC: Invalid write size %d", size);

        // Write Logic
        switch (offset)
        {
        case REG_CMD:
            regs.cmd = (uint32_t)data;
            if (regs.cmd == 1)
                startCompute();
            if (regs.cmd >= 2 && regs.cmd <= 4)
                startDma();
            break;
        case REG_W_ADDR:
            regs.w_addr = data;
            break;
        case REG_W_LEN:
            regs.w_len = (uint32_t)data;
            break;
        case REG_I_ADDR:
            regs.i_addr = data;
            break;
        case REG_I_LEN:
            regs.i_len = (uint32_t)data;
            break;
        case REG_OUT_ADDR:
            regs.out_addr = data;
            break;

        case REG_PREC_I:
            // Dynamic Reconfiguration: Clamp to hardware limits
            regs.prec_i = std::min((uint32_t)data, (uint32_t)maxPrecI);
            DPRINTF(DigitalIMC, "Config: Input Precision set to %d bits\n", regs.prec_i);
            break;
        case REG_PREC_W:
            regs.prec_w = std::min((uint32_t)data, (uint32_t)maxPrecW);
            DPRINTF(DigitalIMC, "Config: Weight Precision set to %d bits\n", regs.prec_w);
            break;
        case REG_SIGN:
            // Bit 0: 0 = Unsigned, 1 = Signed
            regs.sign = (uint32_t)data;
            DPRINTF(DigitalIMC, "Config: Math Mode set to %s\n",
                    (regs.sign & 1) ? "SIGNED" : "UNSIGNED");
            break;
        default:
            break;
        }

        pkt->makeResponse();
        return pioDelay;
    }

    // --- HELPER: Read Packed Bits ---
    // Extracts 'len' bits from a byte vector at arbitrary bit_offset.
    // Crucial for supporting packed weights (correct bandwidth) vs unpacked (wrong bandwidth).
    uint32_t DigitalIMC::readPackedBits(const std::vector<uint8_t> &buf, uint64_t bit_offset, int len)
    {
        uint32_t value = 0;

        // Loop through every bit we need to extract (e.g., len=4 for 4-bit weights)
        for (int i = 0; i < len; i++)
        {

            // 1. Find the physical location of the bit
            uint64_t current_bit = bit_offset + i; // to go over all the bits of the weight
            uint64_t byte_idx = current_bit / 8;   // Which byte contains this bit?
            int bit_in_byte = current_bit % 8;     // Which position (0-7) inside that byte?

            // Safety: Stop if we ran off the end of the buffer
            if (byte_idx >= buf.size())
                break;

            // 2. Extract the single bit
            // Shift right by 'bit_in_byte' to move the target bit to LSB (position 0)
            // AND with 1 to mask off everything else.
            uint8_t bit = (buf[byte_idx] >> bit_in_byte) & 1;

            // 3. Assemble the result
            // Shift the extracted bit to its correct significance in the output value.
            // If i=0 (LSB), shift 0. If i=3 (MSB), shift 3.
            value |= (bit << i);
        }
        return value;
    }

    int32_t DigitalIMC::extendSign(uint32_t val, int bits)
    {
        // If the Most Significant Bit of the precision is 1, it's negative
        if (val & (1 << (bits - 1)))
        {
            // Create a mask for the upper bits and set them to 1
            return (int32_t)(val | (~((1 << bits) - 1)));
        }
        // Otherwise it's positive, just return as is
        return (int32_t)val;
    }
    // --- 1. COMPUTE LOGIC (System-Level + Bit-Serial Proof) ---

    void DigitalIMC::startCompute()
    {
        if (regs.status != 0)
        {
            warn("DigitalIMC: Hardware Busy!\n");
            return;
        }
        regs.status = 1; // Mark Busy

        // 1. Calculate Batch Geometry
        // One Batch = numRows * Input_Precision (in bits)
        uint64_t bits_per_batch = (uint64_t)numRows * regs.prec_i;
        uint64_t bytes_per_batch = (bits_per_batch + 7) / 8; // Round up to bytes

        // 2. Calculate Total Batches based on DMA Length
        // regs.i_len is the total bytes we loaded into Input SRAM
        if (bytes_per_batch == 0)
            bytes_per_batch = 1; // Safety
        total_batches = regs.i_len / bytes_per_batch;

        // Safety Check
        if (total_batches == 0)
            total_batches = 1;

        current_batch_idx = 0; // Reset Counter

        DPRINTF(DigitalIMC, "Start Compute: %d Batches (BatchSize=%lu bytes)\n",
                total_batches, bytes_per_batch);

        // 3. Schedule FIRST Batch
        // Latency per batch = Clock * Precision
        Tick batch_latency = clockPeriod() * regs.prec_i;
        schedule(computeDoneEvent, curTick() + batch_latency);
    }

    void DigitalIMC::computeDone()
    {
        // --- A. CLEAR ACCUMULATORS (Reset itself) ---
        std::fill(internal_accumulators.begin(), internal_accumulators.end(), 0);

        // --- B. CALCULATE OFFSET for Current Batch ---
        // How many rows have we already processed?
        uint64_t row_start_offset = current_batch_idx * numRows;

        // LOOP 1: ROWS (Iterate inputs)
        for (int r = 0; r < numRows; r++)
        {
            // LOGIC CHANGE: Add batch offset to row index
            uint64_t actual_row_index = row_start_offset + r;

            uint64_t i_bit_offset = actual_row_index * regs.prec_i;

            // Safety: Don't read past SRAM end
            if ((i_bit_offset / 8) >= input_sram.size())
                break;
            // 1. Read raw bits
            uint32_t raw_i = readPackedBits(input_sram, i_bit_offset, regs.prec_i);

            // 2. SIGN EXTEND INPUT: Convert from e.g. 4-bit signed to 32-bit signed
            int32_t input_val = (regs.sign & 1) ? extendSign(raw_i, regs.prec_i) : (int32_t)raw_i;

            // LOOP 2: COLUMNS (Iterate weights)
            for (int c = 0; c < numCols; c++)
            {
                // 1. Read the full weight value ONCE
                uint64_t w_index = r * numCols + c;
                uint64_t w_bit_offset = w_index * regs.prec_w;
                // 3. Read raw bits
                uint32_t raw_w = readPackedBits(weight_memory, w_bit_offset, regs.prec_w);

                // 4. SIGN EXTEND WEIGHT: Convert from e.g. 8-bit signed to 32-bit signed
                int32_t weight_val = (regs.sign & 1) ? extendSign(raw_w, regs.prec_w) : (int32_t)raw_w;
                // 2. Perform the Math directly (Integer MAC)
                // Since we already paid the latency penalty in 'startCompute',
                // we can cheat and do the math instantly here.
                // (input * weight) is mathematically identical to the bit-serial sum.
                internal_accumulators[c] += (int64_t)input_val * weight_val;
            }
        }

        // --- D. UPDATE STATS ---
        uint64_t ops_this_batch = (uint64_t)numRows * numCols;
        stats.numMacOps += ops_this_batch;
        stats.numTotalBitOps += ops_this_batch * regs.prec_i * maxPrecW;
        stats.numSramReads += numRows; // Broadcast 1 input row -> 6 cols
        stats.numSramWrites += numCols;

        // ---------------------------------

        // --- WRITEBACK: Internal Accumulators -> Output SRAM ---

        // 1. Calculate Theoretical Max Width (Sun et al.)
        //    Example: 8-bit In + 8-bit W + 32 Rows (5 bits) = 21 bits
        double theoretical_bits = maxPrecI + maxPrecW + std::ceil(std::log2(numRows));

        // 2. Round Up to Nearest Power of 2 (Hardware Alignment)
        //    If bits=21 -> next pow2 is 32.
        //    If bits=13 -> next pow2 is 16.
        //    If bits=7  -> next pow2 is 8.
        int aligned_bits = std::pow(2, std::ceil(std::log2(theoretical_bits)));

        // 3. Enforce Minimum 8 bits (1 Byte)
        if (aligned_bits < 8)
            aligned_bits = 8;

        // 4. Convert to Bytes
        int bytes_per_col = aligned_bits / 8;

        // --- GENERIC SAFETY CHECK ---
        // We define the output container type here.
        // If you ever want 64-bit output, just change uint32_t to uint64_t RIGHT HERE.
        // The check below will automatically adapt.
        typedef uint32_t OutputType;

        if (bytes_per_col > sizeof(OutputType))
        {
            panic("DigitalIMC: Output requires %d bytes, but simulator uses %lu-bit intermediates! "
                  "Please change 'OutputType' typedef in computeDone.",
                  bytes_per_col, sizeof(OutputType) * 8);
        }

        uint64_t write_ptr = current_sram_offset;

        for (int c = 0; c < numCols; c++)
        {
            // Safety Check: Stop if SRAM is full
            if (write_ptr + bytes_per_col > output_sram.size())
            {
                warn("DigitalIMC: Output SRAM Full! Data dropped.\n");
                break;
            }

            uint32_t val = (OutputType)internal_accumulators[c];

            // GENERIC WRITER: Supports 1, 2, 4 bytes automatically
            // We act like a little-endian memory controller
            for (int b = 0; b < bytes_per_col; b++)
            {
                // Take the byte at shift 'b*8'
                output_sram[write_ptr + b] = (val >> (b * 8)) & 0xFF;
            }

            write_ptr += bytes_per_col;

            DPRINTF(DigitalIMC, "Result Col[%d] = %u (Width: %d bits -> Aligned: %d bits -> Stored: %d bytes)\n",
                    c, val, (int)theoretical_bits, aligned_bits, bytes_per_col);
        }

        // --- CRITICAL FIX START ---
        // You MUST save the new position, otherwise the next batch
        // will overwrite the data you just wrote!
        current_sram_offset = write_ptr;

        // --- F. LOOP LOGIC (The "Next Batch" Decision) ---
        current_batch_idx++;

        if (current_batch_idx < total_batches)
        {
            // WE HAVE MORE BATCHES!
            // Schedule the next event immediately after this one finishes
            Tick batch_latency = clockPeriod() * regs.prec_i;
            schedule(computeDoneEvent, curTick() + batch_latency);

            DPRINTF(DigitalIMC, "Batch %d/%d Complete. Scheduling next...\n",
                    current_batch_idx, total_batches);
        }
        else
        {
            // ALL DONE
            regs.status = 0;
            regs.cmd = 0;
            DPRINTF(DigitalIMC, "Compute JOB Complete. Processed %d Batches.\n", total_batches);
        }
    }

    // --- 2. DMA LOGIC (Data Movement) ---

    // --- 2. DMA LOGIC (FIXED) ---
    void DigitalIMC::startDma()
    {
        if (regs.status != 0)
        {
            warn("DigitalIMC: Hardware Busy! DMA request ignored.\n");
            return;
        }

        regs.status = 1; // Mark Busy

        // --- WRITE REQUEST (Output SRAM -> DRAM) ---
        if (regs.cmd == 4)
        {
            uint8_t *src_ptr = output_sram.data();
            Addr dest_addr = regs.out_addr;
            uint32_t len = current_sram_offset; // Flush all valid data

            DPRINTF(DigitalIMC, "DMA WRITE: Output SRAM -> DRAM 0x%lx (Len=%d)\n", dest_addr, len);

            // --- ENERGY STATS UPDATE (NEW) ---
            stats.totalDmaBytesWritten += len;
            // ---------------------------------

            dmaPort.dmaAction(MemCmd::WriteReq, dest_addr, len, &dmaDoneEvent, src_ptr, 0);
            // --- CRITICAL FIX START ---
            // Reset the internal pointer so the SRAM is "Empty" for the next run.
            current_sram_offset = 0;
            // --- CRITICAL FIX END ---
            return; // EXIT FUNCTION HERE!
        }

        // --- READ REQUESTS (DRAM -> Internal) ---
        Addr src_addr = (regs.cmd == 2) ? regs.w_addr : regs.i_addr;
        uint32_t len = (regs.cmd == 2) ? regs.w_len : regs.i_len;
        uint8_t *dst_ptr = nullptr;
        uint64_t max_cap = 0;

        if (regs.cmd == 2)
        {
            // Load Weights
            dst_ptr = weight_memory.data();
            max_cap = weight_memory.size();
            DPRINTF(DigitalIMC, "DMA READ: Loading PACKED Weights from 0x%lx (Len=%d)\n", src_addr, len);
        }
        else if (regs.cmd == 3)
        {
            // Load Inputs
            dst_ptr = input_sram.data();
            max_cap = input_sram.size();
            DPRINTF(DigitalIMC, "DMA READ: Loading Inputs from 0x%lx (Len=%d)\n", src_addr, len);
        }

        if (len > max_cap)
        {
            panic("DigitalIMC: DMA Overflow! Req %d > Capacity %lu", len, max_cap);
        }
        if (len == 0)
        {
            warn("DigitalIMC: Zero-length DMA request.\n");
            regs.status = 0;
            return;
        }
        // --- ENERGY STATS UPDATE (NEW) ---
        stats.totalDmaBytesRead += len;
        // ---------------------------------
        // Trigger Read DMA
        dmaPort.dmaAction(MemCmd::ReadReq, src_addr, len, &dmaDoneEvent, dst_ptr, 0);
    }

    void DigitalIMC::dmaDone()
    {

        // --- DEBUG PROBE START ---
        // If we just finished loading inputs (Cmd 3), print the first few bytes
        if (regs.cmd == 3)
        {
            DPRINTF(DigitalIMC, "DMA DEBUG: Input SRAM[0]=%d, SRAM[1]=%d, SRAM[2]=%d\n",
                    input_sram[0], input_sram[1], input_sram[2]);
        }
        // If we just finished loading weights (Cmd 2), print the first few bytes
        if (regs.cmd == 2)
        {
            DPRINTF(DigitalIMC, "DMA DEBUG: Weight Mem[0]=%d, Mem[1]=%d\n",
                    weight_memory[0], weight_memory[1]);
        }
        // --- DEBUG PROBE END ---

        DPRINTF(DigitalIMC, "DMA Complete.\n");
        regs.status = 0;
        regs.cmd = 0;
    }

} // namespace gem5
