-- ----------------------------------------------------------------------------	
-- FILE: 	data2packets_top.vhd
-- DESCRIPTION:	 
-- DATE:	March 22, 2017
-- AUTHOR(s):	Lime Microsystems
-- REVISIONS:
-- ----------------------------------------------------------------------------	

-- ----------------------------------------------------------------------------
-- Notes:
-- ----------------------------------------------------------------------------

library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

-- ----------------------------------------------------------------------------
-- Entity declaration
-- ----------------------------------------------------------------------------
entity data2packets_top is
   generic(
      dev_family           : string := "Cyclone IV E";
      outbus_width        : integer := 64;
      smpl_buff_rdusedw_w : integer := 11; --bus width in bits 
      pct_buff_wrusedw_w  : integer := 12; --bus width in bits      
      G_DISABLE_14BIT     : boolean := false --generic to disable 14bit sample packing from being implemented ( to save fpga resources ) 
      
   );
   port (
      clk               : in std_logic;
      reset_n           : in std_logic;
      sample_width      : in std_logic_vector(1 downto 0); --"10"-12bit, "01"-14bit, "00"-16bit;
      pct_hdr_0         : in std_logic_vector(63 downto 0);
      pct_hdr_1         : in std_logic_vector(63 downto 0);
      pct_buff_wrusedw  : in std_logic_vector(pct_buff_wrusedw_w-1 downto 0);   
      pct_buff_wrreq    : out std_logic;
      pct_buff_wrdata   : out std_logic_vector(outbus_width-1 downto 0);
      smpl_buff_rdusedw : in std_logic_vector(smpl_buff_rdusedw_w-1 downto 0);
      smpl_buff_rdreq   : out std_logic;
      smpl_buff_rddata  : in std_logic_vector(outbus_width-1 downto 0);
      pct_hdr_cap       : out std_logic
    
        );
end data2packets_top;

-- ----------------------------------------------------------------------------
-- Architecture
-- ----------------------------------------------------------------------------
architecture arch of data2packets_top is
--declare signals,  components here

constant C_OUTBUS_DOUBLE      : boolean := (outbus_width = 128);
--inst0 
signal inst0_pct_buff_wr_dis  : std_logic;
signal inst0_smpl_buff_rdreq  : std_logic;
signal inst0_smpl_rd_size     : std_logic_vector(11 downto 0); 
signal inst0_smpl_rd_size_mod : std_logic_vector(11 downto 0); 
signal inst0_data2packets_done: std_logic;
signal inst0_smpl_buff_rdy    : std_logic;
signal inst0_pct_buff_rdy     : std_logic;

--inst1 
signal inst1_data_in_valid    : std_logic;
signal inst1_data_out         : std_logic_vector(outbus_width-1 downto 0);
signal inst1_data_out_valid   : std_logic;

--isnt2
signal inst2_pct_state        : std_logic_vector(1 downto 0);
signal inst2_pct_wrreq        : std_logic;
signal inst2_pct_q            : std_logic_vector(outbus_width-1 downto 0);
signal inst2_pct_size         : std_logic_vector(pct_buff_wrusedw_w-1 downto 0);
signal inst2_pct_size_mod     : std_logic_vector(pct_buff_wrusedw_w-1 downto 0);

--calculated values
signal smpl_buff_rdusedw_min        : unsigned(smpl_buff_rdusedw_w-1 downto 0);
signal smpl_buff_rdusedw_min_mod    : unsigned(smpl_buff_rdusedw_w-1 downto 0);
signal pct_buff_wrusedw_max_words   : unsigned(pct_buff_wrusedw_w-1 downto 0);
signal pct_buff_wrusedw_max_limit   : unsigned(pct_buff_wrusedw_w-1 downto 0);

--output registers
signal pct_buff_wrdata_reg          : std_logic_vector(outbus_width-1 downto 0);       
signal smpl_buff_rdreq_reg          : std_logic;

--input registers
signal smpl_buff_rdusedw_reg        : std_logic_vector(smpl_buff_rdusedw_w-1 downto 0);

signal pct_hdr_captured             : std_logic;

 
begin



proc_name : process(clk, reset_n)
begin
   if reset_n = '0' then 
      smpl_buff_rdusedw_reg <= (others=>'0');
   elsif (clk'event AND clk='1') then 
      smpl_buff_rdusedw_reg <= smpl_buff_rdusedw;
   end if;
end process;

-- ----------------------------------------------------------------------------
-- To decide what size packets will be formed
-- When sample width is "01"-14bit, it is impossible to pack samples in 4096B 
-- packets so that whole packet contains only integer samples
-- ----------------------------------------------------------------------------

process(clk, reset_n)
begin
   if reset_n = '0' then 
      inst2_pct_size <= (others => '0');
   elsif (clk'event AND clk='1') then
      if sample_width = "01" then 
         inst2_pct_size <= std_logic_vector(to_unsigned(128,inst2_pct_size'length)); --128x64b=1024Bytes
      else 
         inst2_pct_size <= std_logic_vector(to_unsigned(512,inst2_pct_size'length)); --512x64b=4096Bytes
      end if;
      if C_OUTBUS_DOUBLE then
        inst2_pct_size_mod <= '0' & inst2_pct_size(inst2_pct_size'LEFT downto 1);
      else
        inst2_pct_size_mod <= inst2_pct_size;
      end if;
   end if;
end process;


--max words in pct_buff
pct_buff_wrusedw_max_words <= ((pct_buff_wrusedw_w-1) => '1', others=>'0');

--limit to fill up pct_buff
process(clk, reset_n)
begin
   if reset_n = '0' then 
      pct_buff_wrusedw_max_limit <= (others => '0');
   elsif (clk'event AND clk='1') then
      pct_buff_wrusedw_max_limit <= pct_buff_wrusedw_max_words - unsigned(inst2_pct_size_mod);
   end if;
end process;

--pct_buff_rdy signal formation
process(clk, reset_n)
begin
   if reset_n = '0' then 
      inst0_pct_buff_rdy <= '0';
   elsif (clk'event AND clk='1') then
      if unsigned(pct_buff_wrusedw) >= pct_buff_wrusedw_max_limit then 
         inst0_pct_buff_rdy <= '0';
      else
         inst0_pct_buff_rdy <= '1';
      end if;
   end if;
end process;


-- ----------------------------------------------------------------------------
-- To decide how many samples are needed to make full packet
-- ----------------------------------------------------------------------------
process(clk, reset_n)
begin
   if reset_n = '0' then 
      smpl_buff_rdusedw_min <= (others=>'0');
   elsif (clk'event AND clk='1') then 
      if sample_width = "10" then
         smpl_buff_rdusedw_min <= to_unsigned(680, smpl_buff_rdusedw_min'length);
      elsif sample_width = "01" then
         smpl_buff_rdusedw_min <= to_unsigned(144, smpl_buff_rdusedw_min'length);
      else
         smpl_buff_rdusedw_min <= to_unsigned(510, smpl_buff_rdusedw_min'length);
      end if;
   end if;
end process;

-- if double bus is used right shift this value to halve it
smpl_buff_rdusedw_min_mod <= smpl_buff_rdusedw_min when C_OUTBUS_DOUBLE=FALSE else '0' & smpl_buff_rdusedw_min(smpl_buff_rdusedw_min'LEFT downto 1);

process(clk, reset_n)
begin
   if reset_n = '0' then 
      inst0_smpl_buff_rdy <= '0';
   elsif (clk'event AND clk='1') then
      if unsigned(smpl_buff_rdusedw_reg) > smpl_buff_rdusedw_min_mod then 
         inst0_smpl_buff_rdy <= '1';
      else 
         inst0_smpl_buff_rdy <= '0';
      end if;
   end if;
end process;

process(clk, reset_n)
begin
   if reset_n = '0' then 
      inst0_data2packets_done <= '0';
   elsif (clk'event AND clk='1') then
      if inst2_pct_state = "00" then 
         inst0_data2packets_done <= '1';
      else 
         inst0_data2packets_done <= '0';
      end if;
   end if;
end process;

process(clk, reset_n)
begin
   if reset_n = '0' then 
      pct_hdr_captured <= '0';
   elsif (clk'event AND clk='1') then
      if inst2_pct_state = "01" then 
         pct_hdr_captured <= '1';
      else 
         pct_hdr_captured <= '0';
      end if;
   end if;
end process;


process(clk, reset_n)
begin
   if reset_n = '0' then 
      inst0_smpl_rd_size <= (others=>'0');
   elsif (clk'event AND clk='1') then 
      if sample_width = "10" then
         inst0_smpl_rd_size <= std_logic_vector(to_unsigned(680, inst0_smpl_rd_size'length));
      elsif sample_width = "01" then
         inst0_smpl_rd_size <= std_logic_vector(to_unsigned(144, inst0_smpl_rd_size'length));
      else
         inst0_smpl_rd_size <= std_logic_vector(to_unsigned(510, inst0_smpl_rd_size'length));
      end if;
   end if;
end process;

-- if bus is doubled, shift inst0_smpl_rd_size one bit right to halve the number of reads required to form a packet
inst0_smpl_rd_size_mod <= inst0_smpl_rd_size when C_OUTBUS_DOUBLE = false else ('0' & inst0_smpl_rd_size(inst0_smpl_rd_size'LEFT downto 1));

-- ----------------------------------------------------------------------------
-- State machine instance
-- ----------------------------------------------------------------------------
data2packets_fsm_inst0 : entity work.data2packets_fsm
   port map (
      clk               => clk,
      reset_n           => reset_n,
      pct_buff_rdy      => inst0_pct_buff_rdy,
      pct_buff_wr_dis   => inst0_pct_buff_wr_dis,
      smpl_rd_size      => inst0_smpl_rd_size_mod,
      smpl_buff_rdy     => inst0_smpl_buff_rdy,
      smpl_buff_rdreq   => inst0_smpl_buff_rdreq,
      data2packets_done => inst0_data2packets_done   
        );
        
 
process(clk, reset_n)
begin
   if reset_n = '0' then 
      inst1_data_in_valid <= '0';
   elsif (clk'event AND clk='1') then 
      inst1_data_in_valid <= inst0_smpl_buff_rdreq;
   end if;
end process;
 

-- ----------------------------------------------------------------------------
-- Different implementations for different bus widths
-- ----------------------------------------------------------------------------

BUS64 : if C_OUTBUS_DOUBLE = false generate 

-- ----------------------------------------------------------------------------
-- Bit packing instance
-- ----------------------------------------------------------------------------
bit_pack_inst1 : entity work.bit_pack
  generic map (
        G_PORT_WIDTH    => outbus_width,
        G_DISABLE_14BIT => G_DISABLE_14BIT
        )
  port map (
        clk             => clk,
        reset_n         => reset_n,
        data_in         => smpl_buff_rddata,
        data_in_valid   => inst1_data_in_valid,
        sample_width    => sample_width,
        data_out        => inst1_data_out,
        data_out_valid  => inst1_data_out_valid
        );

-- ----------------------------------------------------------------------------
-- Packet formation instance
-- ----------------------------------------------------------------------------        

data2packets_inst2 : entity work.data2packets
   generic map(
      pct_size_w        => pct_buff_wrusedw_w
   )
   port map(
      clk               => clk,
      reset_n           => reset_n,
      pct_size          => inst2_pct_size_mod,
      pct_hdr_0         => pct_hdr_0,
      pct_hdr_1         => pct_hdr_1,
      pct_data          => inst1_data_out,
      pct_data_wrreq    => inst1_data_out_valid,
      pct_state         => inst2_pct_state,
      pct_wrreq         => inst2_pct_wrreq,
      pct_q             => inst2_pct_q    
        );
end generate;

BUS128 : if C_OUTBUS_DOUBLE = true generate

    -- ----------------------------------------------------------------------------
    -- Bit packing instance nr0
    -- ----------------------------------------------------------------------------
    bit_pack_inst1 : entity work.bit_pack
    generic map (
        G_PORT_WIDTH    => outbus_width,
        G_DISABLE_14BIT => G_DISABLE_14BIT
        )
      port map (
        clk             => clk,
        reset_n         => reset_n,
        data_in         => smpl_buff_rddata,--(63 downto 0),
        data_in_valid   => inst1_data_in_valid,
        sample_width    => sample_width,
        data_out        => inst1_data_out,--(63 downto 0),
        data_out_valid  => inst1_data_out_valid
        );
        
--    -- ----------------------------------------------------------------------------
--    -- Bit packing instance nr0
--    -- ----------------------------------------------------------------------------
--    bit_pack_inst1_1 : entity work.bit_pack
--      port map (
--        clk             => clk,
--        reset_n         => reset_n,
--        data_in         => smpl_buff_rddata(127 downto 64),
--        data_in_valid   => inst1_data_in_valid,
--        sample_width    => sample_width,
--        data_out        => inst1_data_out(127 downto 64),
--        data_out_valid  => open--inst1_data_out_valid
--        );

    -- ----------------------------------------------------------------------------
    -- Packet formation instance
    -- ----------------------------------------------------------------------------        

    -- local declaration for doubled bus
    double_bus : block
    
--        signal inst1_data_out_mod       : std_logic_vector(127 downto 0);
--        signal inst1_data_out_valid_mod : std_logic;
--        signal rdusedw                  : std_logic_vector(smpl_buff_rdusedw_w-1 downto 0);
--        attribute MARK_DEBUG of inst1_data_out_mod        : signal is "TRUE";
--        attribute MARK_DEBUG of inst1_data_out_valid_mod  : signal is "TRUE";
--        attribute MARK_DEBUG of rdusedw                   : signal is "TRUE";
    
    begin
               
--           smpl_fifo_inst1 : entity work.fifo_inst
--              generic map(
--                  dev_family      => dev_family, 
--                  wrwidth         => 64,
--                  wrusedw_witdth  => smpl_buff_rdusedw_w+1,
--                  rdwidth         => 128,
--                  rdusedw_width   => smpl_buff_rdusedw_w,
--                  show_ahead      => "ON"
--              ) 
            
--              port map(
--                  --input ports 
--                  reset_n        => reset_n,
--                  wrclk          => clk,
--                  wrreq          => inst1_data_out_valid,
--                  data           => inst1_data_out,
--                  wrfull         => open,
--                  wrempty        => open,
--                  wrusedw        => open,
--                  rdclk          => clk,
--                  rdreq          => not inst1_data_out_valid_mod,
--                  q              => inst1_data_out_mod,
--                  rdempty        => inst1_data_out_valid_mod,
--                  rdusedw        => rdusedw  
--                    );
        
        
        
        data2packets_inst2_1 : entity work.data2packets128
           generic map(
              pct_size_w        => pct_buff_wrusedw_w
           )
           port map(
              clk               => clk,
              reset_n           => reset_n,
              -- right shift by one bit to halve this value
              pct_size          => inst2_pct_size_mod,--'0' & inst2_pct_size(inst2_pct_size'LEFT downto 1), 
              pct_hdr           => pct_hdr_1 & pct_hdr_0,
              pct_data          => inst1_data_out,
              pct_data_wrreq    => inst1_data_out_valid,
              pct_state         => inst2_pct_state,
              pct_wrreq         => inst2_pct_wrreq,
              pct_q             => inst2_pct_q    
                );
            
    end block double_bus;
end generate;



-- ----------------------------------------------------------------------------
-- Output registers
-- ----------------------------------------------------------------------------       
process(clk, reset_n)
begin
   if reset_n = '0' then 
      pct_buff_wrdata_reg <= (others=> '0');
      smpl_buff_rdreq_reg <= '0';
   elsif (clk'event AND clk='1') then 
      pct_buff_wrdata_reg <= inst2_pct_q;
      smpl_buff_rdreq_reg <= inst2_pct_wrreq AND inst0_pct_buff_wr_dis; 
   end if;
end process;

-- to output ports 
pct_buff_wrdata   <= pct_buff_wrdata_reg;  
pct_buff_wrreq    <= smpl_buff_rdreq_reg;
smpl_buff_rdreq   <= inst0_smpl_buff_rdreq;
pct_hdr_cap       <= pct_hdr_captured;
     


  
end arch;   





