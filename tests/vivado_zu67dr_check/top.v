module top (
    input  wire clk,
    input  wire rst_n,
    output wire led
);
    reg [23:0] counter = 24'd0;

    always @(posedge clk) begin
        if (!rst_n) begin
            counter <= 24'd0;
        end else begin
            counter <= counter + 24'd1;
        end
    end

    assign led = counter[23];
endmodule
