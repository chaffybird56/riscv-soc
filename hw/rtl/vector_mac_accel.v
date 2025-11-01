// Simple Wishbone-mapped Vector MAC / 1D Convolution Accelerator
// - Registers @ 0x0000..0x00FF
// - Input A   @ 0x1000..0x1FFF (word-addressed)
// - Input B   @ 0x2000..0x2FFF (word-addressed)
// - Output    @ 0x3000..0x3FFF (word-addressed)

module vector_mac_accel #(
    parameter integer ADDR_WIDTH = 16,
    parameter integer DATA_WIDTH = 32,
    parameter integer MAX_LEN    = 4096
)(
    input  wire                          clk,
    input  wire                          rst,

    // Wishbone (classic)
    input  wire                          wb_cyc,
    input  wire                          wb_stb,
    input  wire                          wb_we,
    input  wire [DATA_WIDTH/8-1:0]       wb_sel,
    input  wire [ADDR_WIDTH-1:0]         wb_adr, // word address within region
    input  wire [DATA_WIDTH-1:0]         wb_dat_w,
    output reg  [DATA_WIDTH-1:0]         wb_dat_r,
    output reg                           wb_ack
);

    // ---------------------------
    // Memory map decode
    // ---------------------------
    wire sel_regs = (wb_adr[15:8] == 8'h00); // 0x00xx
    wire sel_A    = (wb_adr[15:12] == 4'h1); // 0x1xxx
    wire sel_B    = (wb_adr[15:12] == 4'h2); // 0x2xxx
    wire sel_OUT  = (wb_adr[15:12] == 4'h3); // 0x3xxx

    wire [11:0] idx_mem = wb_adr[11:0];     // up to 4096 entries per bank
    wire [7:0]  idx_reg = wb_adr[7:0];      // registers as words

    // ---------------------------
    // Registers
    // ---------------------------
    // CTRL: bit0 start, bit1 clear, bit2 mode (0=dot, 1=conv)
    reg  [DATA_WIDTH-1:0] reg_ctrl;
    // STATUS: bit0 busy, bit1 done
    reg  [DATA_WIDTH-1:0] reg_status;
    reg  [DATA_WIDTH-1:0] reg_length;      // dot: length, conv: output length
    reg  [DATA_WIDTH-1:0] reg_klen;        // conv: kernel length
    reg  [DATA_WIDTH-1:0] reg_cycles_lo;
    reg  [DATA_WIDTH-1:0] reg_cycles_hi;
    reg  [DATA_WIDTH-1:0] reg_scale_shift; // arithmetic right shift after mul

    wire start_req = sel_regs && wb_we && (idx_reg == 8'd0) && wb_dat_w[0];
    wire clear_req = sel_regs && wb_we && (idx_reg == 8'd0) && wb_dat_w[1];
    wire mode_conv = reg_ctrl[2];

    // ---------------------------
    // Memories
    // ---------------------------
    reg signed [DATA_WIDTH-1:0] memA  [0:MAX_LEN-1];
    reg signed [DATA_WIDTH-1:0] memB  [0:MAX_LEN-1];
    reg signed [DATA_WIDTH-1:0] memOUT[0:MAX_LEN-1];

    // ---------------------------
    // Wishbone ACK (1-cycle)
    // ---------------------------
    always @(posedge clk) begin
        if (rst) begin
            wb_ack <= 1'b0;
        end else begin
            if (wb_cyc && wb_stb && !wb_ack) begin
                wb_ack <= 1'b1;
            end else begin
                wb_ack <= 1'b0;
            end
        end
    end

    // ---------------------------
    // Wishbone read mux
    // ---------------------------
    always @(*) begin
        wb_dat_r = {DATA_WIDTH{1'b0}};
        if (sel_regs) begin
            case (idx_reg)
                8'd0: wb_dat_r = reg_ctrl;
                8'd1: wb_dat_r = reg_status;
                8'd2: wb_dat_r = reg_length;
                8'd3: wb_dat_r = reg_klen;
                8'd4: wb_dat_r = reg_cycles_lo;
                8'd5: wb_dat_r = reg_cycles_hi;
                8'd6: wb_dat_r = reg_scale_shift;
                default: wb_dat_r = {DATA_WIDTH{1'b0}};
            endcase
        end else if (sel_A) begin
            wb_dat_r = memA[idx_mem];
        end else if (sel_B) begin
            wb_dat_r = memB[idx_mem];
        end else if (sel_OUT) begin
            wb_dat_r = memOUT[idx_mem];
        end
    end

    // ---------------------------
    // Wishbone writes
    // ---------------------------
    integer i;
    always @(posedge clk) begin
        if (rst) begin
            reg_ctrl        <= {DATA_WIDTH{1'b0}};
            reg_status      <= {DATA_WIDTH{1'b0}};
            reg_length      <= {DATA_WIDTH{1'b0}};
            reg_klen        <= {DATA_WIDTH{1'b0}};
            reg_cycles_lo   <= {DATA_WIDTH{1'b0}};
            reg_cycles_hi   <= {DATA_WIDTH{1'b0}};
            reg_scale_shift <= 32'd15; // default Q17.15-style
        end else begin
            if (wb_cyc && wb_stb && wb_we) begin
                if (sel_regs) begin
                    case (idx_reg)
                        8'd0: reg_ctrl        <= wb_dat_w; // start/clear/mode bits sampled below
                        8'd2: reg_length      <= wb_dat_w;
                        8'd3: reg_klen        <= wb_dat_w;
                        8'd6: reg_scale_shift <= wb_dat_w;
                        default: ;
                    endcase
                end else if (sel_A) begin
                    memA[idx_mem] <= wb_dat_w;
                end else if (sel_B) begin
                    memB[idx_mem] <= wb_dat_w;
                end else if (sel_OUT) begin
                    memOUT[idx_mem] <= wb_dat_w;
                end
            end

            // Clear request
            if (clear_req) begin
                reg_status    <= {DATA_WIDTH{1'b0}};
                reg_cycles_lo <= {DATA_WIDTH{1'b0}};
                reg_cycles_hi <= {DATA_WIDTH{1'b0}};
            end
        end
    end

    // ---------------------------
    // Compute engine
    // ---------------------------
    localparam S_IDLE = 2'd0;
    localparam S_RUN  = 2'd1;
    localparam S_DONE = 2'd2;

    reg [1:0] state;

    reg [31:0] length;
    reg [31:0] klen;
    reg [31:0] shift_amt;

    reg [31:0] i_idx;      // dot loop idx
    reg [31:0] o_idx;      // conv output position
    reg [31:0] k_idx;      // conv kernel idx

    reg signed [63:0] acc_sum;
    reg [63:0] cycle_counter;

    wire busy = (state == S_RUN);
    wire done = (state == S_DONE);

    always @(posedge clk) begin
        if (rst) begin
            state          <= S_IDLE;
            acc_sum        <= 64'sd0;
            i_idx          <= 32'd0;
            o_idx          <= 32'd0;
            k_idx          <= 32'd0;
            cycle_counter  <= 64'd0;
            reg_status     <= 32'd0;
            reg_cycles_lo  <= 32'd0;
            reg_cycles_hi  <= 32'd0;
        end else begin
            case (state)
                S_IDLE: begin
                    reg_status[0] <= 1'b0; // busy=0
                    reg_status[1] <= 1'b0; // done=0
                    if (start_req) begin
                        // snapshot parameters
                        length    <= reg_length;
                        klen      <= reg_klen;
                        shift_amt <= reg_scale_shift[5:0];
                        acc_sum   <= 64'sd0;
                        i_idx     <= 32'd0;
                        o_idx     <= 32'd0;
                        k_idx     <= 32'd0;
                        cycle_counter <= 64'd0;
                        reg_status[0] <= 1'b1; // busy=1
                        state <= S_RUN;
                    end
                end

                S_RUN: begin
                    cycle_counter <= cycle_counter + 64'd1;
                    if (mode_conv) begin
                        // y[o_idx] = sum_{k=0..klen-1} A[o_idx+k] * B[k]
                        if (o_idx < length) begin
                            if (k_idx < klen) begin
                                // One MAC per cycle
                                acc_sum <= acc_sum + (($signed(memA[o_idx + k_idx]) * $signed(memB[k_idx])) >>> shift_amt);
                                k_idx   <= k_idx + 32'd1;
                            end else begin
                                // write result and advance output position
                                memOUT[o_idx] <= acc_sum[31:0];
                                acc_sum <= 64'sd0;
                                k_idx   <= 32'd0;
                                o_idx   <= o_idx + 32'd1;
                            end
                        end else begin
                            // done
                            reg_cycles_lo <= cycle_counter[31:0];
                            reg_cycles_hi <= cycle_counter[63:32];
                            reg_status[0] <= 1'b0; // busy=0
                            reg_status[1] <= 1'b1; // done=1
                            state <= S_DONE;
                        end
                    end else begin
                        // dot = sum_{i=0..length-1} A[i]*B[i]
                        if (i_idx < length) begin
                            acc_sum <= acc_sum + (($signed(memA[i_idx]) * $signed(memB[i_idx])) >>> shift_amt);
                            i_idx   <= i_idx + 32'd1;
                        end else begin
                            memOUT[0] <= acc_sum[31:0];
                            reg_cycles_lo <= cycle_counter[31:0];
                            reg_cycles_hi <= cycle_counter[63:32];
                            reg_status[0] <= 1'b0; // busy=0
                            reg_status[1] <= 1'b1; // done=1
                            state <= S_DONE;
                        end
                    end
                end

                S_DONE: begin
                    // Wait for clear to return to IDLE
                    if (clear_req) begin
                        reg_status[1] <= 1'b0; // done=0
                        state <= S_IDLE;
                    end
                end

                default: state <= S_IDLE;
            endcase
        end
    end

endmodule


