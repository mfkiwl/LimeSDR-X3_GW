-- ----------------------------------------------------------------------------	
-- FILE:    rx_path_top.vhd
-- DESCRIPTION:   describe file
-- DATE: March 27, 2017
-- AUTHOR(s):  Lime Microsystems
-- REVISIONS:
-- ----------------------------------------------------------------------------	
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

-- ----------------------------------------------------------------------------
-- Entity declaration
-- ----------------------------------------------------------------------------
entity rx_path_top is
   generic( 
      dev_family           : string := "Cyclone IV E";
      iq_width             : integer := 12;
      invert_input_clocks  : string := "OFF";
      smpl_buff_rdusedw_w  : integer := 11; --bus width in bits 
      pct_buff_wrusedw_w   : integer := 12;  --bus width in bits 
      outbus_width         : integer := 64;
      G_DISABLE_14BIT      : boolean := false -- generic to disable 14 bit packing modules
      );
   port (
      clk                  : in std_logic;
      reset_n              : in std_logic;
      test_ptrn_en         : in std_logic;
      --Mode settings
      sample_width         : in std_logic_vector(1 downto 0); --"10"-12bit, "01"-14bit, "00"-16bit;
      mode                 : in std_logic; -- JESD207: 1; TRXIQ: 0
      trxiqpulse           : in std_logic; -- trxiqpulse on: 1; trxiqpulse off: 0
      ddr_en               : in std_logic; -- DDR: 1; SDR: 0
      mimo_en              : in std_logic; -- SISO: 1; MIMO: 0
      ch_en                : in std_logic_vector(1 downto 0); --"01" - Ch. A, "10" - Ch. B, "11" - Ch. A and Ch. B. 
      fidm                 : in std_logic; -- External Frame ID mode. Frame start at fsync = 0, when 0. Frame start at fsync = 1, when 1.
      --RX FIFO for IQ samples
      smpl_fifo_wrreq      : in std_logic;
      smpl_fifo_data       : in std_logic_vector(iq_width*4-1 downto 0);
      smpl_fifo_wrfull     : out std_logic;
      --Packet FIFO ports 
      pct_fifo_wusedw      : in std_logic_vector(pct_buff_wrusedw_w-1 downto 0);
      pct_fifo_wrreq       : out std_logic;
      pct_fifo_wdata       : out std_logic_vector(outbus_width-1 downto 0);
      pct_hdr_cap          : out std_logic;
      --sample nr
      clr_smpl_nr          : in std_logic;
      ld_smpl_nr           : in std_logic;
      smpl_nr_in           : in std_logic_vector(63 downto 0);
      smpl_nr_cnt          : out std_logic_vector(63 downto 0);
      smpl_nr_cnt_en       : in  std_logic;
      --flag control
      tx_pct_loss          : in std_logic;
      tx_pct_loss_clr      : in std_logic
     
   );
end rx_path_top;

-- ----------------------------------------------------------------------------
-- Architecture
-- ----------------------------------------------------------------------------
architecture arch of rx_path_top is
--declare signals,  components here
--sync registers
signal test_ptrn_en_sync      : std_logic;
signal reset_n_sync           : std_logic;
signal tx_pct_loss_sync       : std_logic;
signal tx_pct_loss_clr_sync   : std_logic;
signal sample_width_sync      : std_logic_vector(1 downto 0); 
signal mode_sync              : std_logic;
signal trxiqpulse_sync        : std_logic; 
signal ddr_en_sync            : std_logic; 
signal mimo_en_sync           : std_logic;
signal ch_en_sync             : std_logic_vector(1 downto 0);
signal fidm_sync              : std_logic;
signal clr_smpl_nr_sync       : std_logic;
signal ld_smpl_nr_sync        : std_logic;
signal smpl_nr_in_sync        : std_logic_vector(63 downto 0);	

--inst0 
signal inst0_fifo_wrreq       : std_logic;
signal inst0_fifo_wdata       : std_logic_vector(iq_width*4-1 downto 0);

--inst1
signal inst1_wrfull           : std_logic;
signal inst1_q                : std_logic_vector(iq_width*(outbus_width/16)-1 downto 0);
signal inst1_rdusedw          : std_logic_vector(smpl_buff_rdusedw_w-1 downto 0);
--inst2
signal inst2_pct_hdr_0        : std_logic_vector(63 downto 0);
signal inst2_pct_hdr_1        : std_logic_vector(63 downto 0);
signal inst2_smpl_buff_rdreq  : std_logic;
signal inst2_smpl_buff_rddata : std_logic_vector(outbus_width-1 downto 0);
--inst3
signal inst3_q                : std_logic_vector(63 downto 0);

--internal signals
type my_array is array (0 to 5) of std_logic_vector(63 downto 0);
signal delay_chain   : my_array;

signal tx_pct_loss_detect     : std_logic;


begin

sync_reg0 : entity work.sync_reg 
port map(clk, '1', reset_n, reset_n_sync);
 
sync_reg1 : entity work.sync_reg 
port map(clk, '1', tx_pct_loss, tx_pct_loss_sync);
 
sync_reg2 : entity work.sync_reg 
port map(clk, '1', tx_pct_loss_clr, tx_pct_loss_clr_sync);

sync_reg3 : entity work.sync_reg 
port map(clk, '1', mode, mode_sync);

sync_reg4 : entity work.sync_reg 
port map(clk, '1', trxiqpulse, trxiqpulse_sync);

sync_reg5 : entity work.sync_reg 
port map(clk, '1', ddr_en, ddr_en_sync);

sync_reg6 : entity work.sync_reg 
port map(clk, '1', mimo_en, mimo_en_sync);

sync_reg7 : entity work.sync_reg 
port map(clk, '1', fidm, fidm_sync);

sync_reg8 : entity work.sync_reg 
port map(clk, '1', clr_smpl_nr, clr_smpl_nr_sync);

sync_reg9 : entity work.sync_reg 
port map(clk, '1', ld_smpl_nr, ld_smpl_nr_sync);

sync_reg10 : entity work.sync_reg 
port map(clk, '1', test_ptrn_en, test_ptrn_en_sync);

bus_sync_reg0 : entity work.bus_sync_reg
generic map (2)
port map(clk, '1', sample_width, sample_width_sync);

bus_sync_reg1 : entity work.bus_sync_reg
generic map (2)
port map(clk, '1', ch_en, ch_en_sync);

bus_sync_reg2 : entity work.bus_sync_reg
generic map (64)
port map(clk, '1', smpl_nr_in, smpl_nr_in_sync);


 
--samples are placed to MSb LSb ar filled with zeros 
--inst2_smpl_buff_rddata <=  inst1_q(47 downto 36) & "0000" & 
--                           inst1_q(35 downto 24) & "0000" & 
--                           inst1_q(23 downto 12) & "0000" & 
--                           inst1_q(11 downto 0) & "0000";
doublebus : if outbus_width = 128 generate
----------------------------------------------------------------------------
-- FIFO for storing samples
-- ----------------------------------------------------------------------------       
smpl_fifo_inst1 : entity work.fifo_inst
  generic map(
      dev_family      => dev_family, 
      wrwidth         => (iq_width*4), 
      wrusedw_witdth  => smpl_buff_rdusedw_w,
      rdwidth         => iq_width*8, 
      rdusedw_width   => smpl_buff_rdusedw_w-1, 
      show_ahead      => "OFF"
  ) 

  port map(
      --input ports 
      reset_n        => reset_n_sync,
      wrclk          => clk,
      wrreq          => smpl_fifo_wrreq,
      data           => smpl_fifo_data,
      wrfull         => smpl_fifo_wrfull,
      wrempty        => open,
      wrusedw        => open,
      rdclk          => clk,
      rdreq          => inst2_smpl_buff_rdreq,
      q              => inst1_q,
      rdempty        => open,
      rdusedw        => inst1_rdusedw(inst1_rdusedw'LEFT-1 downto 0)  
        );
      inst1_rdusedw(inst1_rdusedw'LEFT) <= '0';



    long_gen: for I in 1 to 8 generate
        code_block : block
            constant up_lim : integer := (16*I);
            constant lo_lim : integer := (16*I-1);
        begin
            -- this simply assigns samples from inst1_q to inst2_smpl_buff_rddata and pads unused bits
            -- e.g. if iq_width is 12
            -- inst2_smpl_buff_rddata(15 downto 4) <= inst1_q(11 downto 0);
            -- inst2_smpl_buff_rddata(3  downto 0) <= (others => '0');
            inst2_smpl_buff_rddata(up_lim-1 downto up_lim-iq_width) <= inst1_q(iq_width*I-1 downto iq_width*(I-1));
            inst2_smpl_buff_rddata(up_lim-iq_width-1 downto lo_lim) <= (others => '0');
        end block;    
    end generate;
end generate;

singlebus : if outbus_width = 64 generate

----------------------------------------------------------------------------
-- FIFO for storing samples
-- ----------------------------------------------------------------------------       
smpl_fifo_inst1 : entity work.fifo_inst
  generic map(
      dev_family      => dev_family, 
      wrwidth         => (iq_width*4), 
      wrusedw_witdth  => smpl_buff_rdusedw_w,
      rdwidth         => (iq_width*4),
      rdusedw_width   => smpl_buff_rdusedw_w,
      show_ahead      => "OFF"
  ) 

  port map(
      --input ports 
      reset_n        => reset_n_sync,
      wrclk          => clk,
      wrreq          => smpl_fifo_wrreq,
      data           => smpl_fifo_data,
      wrfull         => smpl_fifo_wrfull,
      wrempty        => open,
      wrusedw        => open,
      rdclk          => clk,
      rdreq          => inst2_smpl_buff_rdreq,
      q              => inst1_q,
      rdempty        => open,
      rdusedw        => inst1_rdusedw  
        );
        
    long_gen: for I in 1 to 4 generate
        code_block : block
            constant up_lim : integer := (16*I);
            constant lo_lim : integer := (16*I-1);
        begin
            -- this simply assigns samples from inst1_q to inst2_smpl_buff_rddata and pads unused bits
            -- e.g. if iq_width is 12
            -- inst2_smpl_buff_rddata(15 downto 4) <= inst1_q(11 downto 0);
            -- inst2_smpl_buff_rddata(3  downto 0) <= (others => '0');
            inst2_smpl_buff_rddata(up_lim-1 downto up_lim-iq_width) <= inst1_q(iq_width*I-1 downto iq_width*(I-1));
            inst2_smpl_buff_rddata(up_lim-iq_width-1 downto lo_lim) <= (others => '0');
        end block;    
    end generate;
end generate;
    
--packet reserved bits  
  inst2_pct_hdr_0(15 downto 0)   <="000000000000" & tx_pct_loss_sync & pct_fifo_wusedw(pct_buff_wrusedw_w-1 downto pct_buff_wrusedw_w-3);
  inst2_pct_hdr_0(31 downto 16)  <=x"0201";
  inst2_pct_hdr_0(47 downto 32)  <=x"0403";
  inst2_pct_hdr_0(63 downto 48)  <=x"0605";
        
        
-- ----------------------------------------------------------------------------
-- Instance for packing samples to packets
-- ----------------------------------------------------------------------------       
data2packets_top_inst2 : entity work.data2packets_top
   generic map(
      dev_family          => dev_family,
      smpl_buff_rdusedw_w => smpl_buff_rdusedw_w,  --bus width in bits 
      pct_buff_wrusedw_w  => pct_buff_wrusedw_w,   --bus width in bits            
      outbus_width        => outbus_width,          --output bus width (valid values are 64 and 128) 
      G_DISABLE_14BIT     => G_DISABLE_14BIT       -- generic to disable 14bit sample packing modules
   )
   port map(
      clk               => clk,
      reset_n           => reset_n_sync,
      sample_width      => sample_width_sync,
      pct_hdr_0         => inst2_pct_hdr_0,
      pct_hdr_1         => inst2_pct_hdr_1,
      pct_buff_wrusedw  => pct_fifo_wusedw,
      pct_buff_wrreq    => pct_fifo_wrreq,
      pct_buff_wrdata   => pct_fifo_wdata,
      pct_hdr_cap       => pct_hdr_cap,
      smpl_buff_rdusedw => inst1_rdusedw,
      smpl_buff_rdreq   => inst2_smpl_buff_rdreq,
      smpl_buff_rddata  => inst2_smpl_buff_rddata   
        );
        
-- ----------------------------------------------------------------------------
-- Instance for packing sample counter for packet forming
-- ----------------------------------------------------------------------------        
smpl_cnt_inst3 : entity work.smpl_cnt
   generic map(
      G_DOUBLEBUS => (outbus_width = 128),
      cnt_width   => 64
   )
   port map(

      clk         => clk,
      reset_n     => reset_n_sync,
      mode        => mode_sync,
      trxiqpulse  => trxiqpulse_sync,
      ddr_en      => ddr_en_sync,
      mimo_en     => mimo_en_sync,
      ch_en       => ch_en_sync,
      sclr        => clr_smpl_nr_sync,
      sload       => ld_smpl_nr_sync,
      data        => smpl_nr_in_sync,
      cnt_en      => inst2_smpl_buff_rdreq,
      q           => inst3_q        
        );
 
-- ----------------------------------------------------------------------------
-- Instance for sample counter
-- ----------------------------------------------------------------------------        
smpl_cnt_inst4 : entity work.iq_smpl_cnt
   generic map(
      G_DOUBLEBUS => false,--(outbus_width = 128),
      cnt_width   => 64
   )
   port map(

      clk         => clk,
      reset_n     => reset_n_sync,
      mode        => mode_sync,
      trxiqpulse  => trxiqpulse_sync,
      ddr_en      => ddr_en_sync,
      mimo_en     => mimo_en_sync,
      ch_en       => ch_en_sync,
      sclr        => clr_smpl_nr_sync,
      sload       => ld_smpl_nr_sync,
      data        => smpl_nr_in_sync,
      cnt_en      => smpl_nr_cnt_en,
      q           => smpl_nr_cnt        
        );
        
-- ----------------------------------------------------------------------------
-- There is 6 clock cycle latency from smpl_fifo_inst1 to packet formation
-- and smpl_cnt has to be delayed 6 cycles
-- ----------------------------------------------------------------------------        
delay_registers : process(clk, reset_n)
begin
   if reset_n = '0' then 
      delay_chain <= (others=>(others=>'0'));
   elsif (clk'event AND clk='1') then 
      for i in 0 to 5 loop
         if i=0 then 
            delay_chain(i) <= inst3_q;
         else 
            delay_chain(i) <= delay_chain(i-1);
         end if;
      end loop;
   end if;
end process;
        
 inst2_pct_hdr_1 <=  delay_chain(5);      
  
end arch;   





