-- ----------------------------------------------------------------------------
-- FILE:          DPDTopWrapper.vhd
-- DESCRIPTION:   Top file for DPD CFR and FIR modules
-- DATE:          10:55 AM Friday, December 19, 2018
-- AUTHOR(s):     Lime Microsystems
-- REVISIONS:
-- ----------------------------------------------------------------------------

LIBRARY ieee;
USE ieee.std_logic_1164.ALL;
USE ieee.numeric_std.ALL;
USE work.memcfg_pkg.ALL;

-- ----------------------------------------------------------------------------
-- Entity declaration
-- ----------------------------------------------------------------------------
ENTITY DPDTopWrapper IS

   GENERIC (
   g_ADPDCFG_START_ADDR   : integer := 416;
   g_CFR0CFG_START_ADDR   : integer := 448;
   g_CFR1CFG_START_ADDR   : integer := 512;
   g_FIR0CFG_START_ADDR   : integer := 576;
   g_FIR1CFG_START_ADDR   : integer := 640;
   DPD_enable : INTEGER := 1  -- B.J.
   );

   PORT (
      clk : IN std_logic;
      reset_n : IN std_logic;
      mem_reset_n : IN std_logic;
      sleep : IN std_logic;
      from_memcfg : IN t_FROM_MEMCFG;

      ch_en : IN std_logic_vector(1 DOWNTO 0);
      sdin : IN std_logic;
      sclk : IN std_logic;
      sen : IN std_logic;
      sdout : OUT std_logic;

      data_req : OUT std_logic;
      data_valid : OUT std_logic;
      --data_req_empty: in std_logic;

      diq_in : IN std_logic_vector(63 DOWNTO 0);
      diq_out : OUT std_logic_vector(63 DOWNTO 0);

      xp_ai, xp_aq, xp_bi, xp_bq : OUT std_logic_vector(15 DOWNTO 0);
      yp_ai, yp_aq, yp_bi, yp_bq : OUT std_logic_vector(15 DOWNTO 0);

      cap_en, cap_cont_en, cap_resetn : OUT std_logic;
      cap_size : OUT std_logic_vector(15 DOWNTO 0);
      PAEN0, PAEN1, DCEN0, DCEN1 : OUT std_logic;
      rf_sw : OUT std_logic_vector(2 DOWNTO 0);
	   reset_n_soft: out std_logic;
	   tx_en, capture_en, reset_n_software, lms3_monitoring: out std_logic
   );
END DPDTopWrapper;

-- ----------------------------------------------------------------------------
-- Architecture
-- ----------------------------------------------------------------------------
ARCHITECTURE arch OF DPDTopWrapper IS

   COMPONENT DPDTop IS
      GENERIC(
      ADPDCFG_START_ADDR   : integer := 416;
      CFR0CFG_START_ADDR   : integer := 448;
      CFR1CFG_START_ADDR   : integer := 512;
      FIR0CFG_START_ADDR   : integer := 576;
      FIR1CFG_START_ADDR   : integer := 640;
      DPD_enable : INTEGER := 1  -- B.J.
      );

      PORT (
         clk_X2 : IN std_logic; -- 122.88 MHz
         reset_n : IN std_logic;
         sleep : IN std_logic;
         mem_reset_n : IN std_logic;
         from_memcfg : IN t_FROM_MEMCFG;
         ai_in, aq_in, bi_in, bq_in : IN std_logic_vector(15 DOWNTO 0);
         sdin : IN std_logic;
         sclk : IN std_logic;
         sen : IN std_logic;
         sdout : OUT std_logic;
         xp_ai, xp_aq, xp_bi, xp_bq : OUT std_logic_vector(15 DOWNTO 0);
         yp_ai, yp_aq, yp_bi, yp_bq : OUT std_logic_vector(15 DOWNTO 0);
         xen, yen : OUT std_logic;
         cap_en, cap_cont_en, cap_resetn : OUT std_logic;
         cap_size : OUT std_logic_vector(15 DOWNTO 0);
         PAEN0, PAEN1, DCEN0, DCEN1 : OUT std_logic;
         rf_sw : OUT std_logic_vector(2 DOWNTO 0);
		 reset_n2: out std_logic;
	     tx_en, capture_en, reset_n_software, lms3_monitoring, fix_mimo, dpdtop_en: out std_logic
      );
   END COMPONENT DPDTop;

   SIGNAL ai_in : std_logic_vector(15 DOWNTO 0);
   SIGNAL aq_in : std_logic_vector(15 DOWNTO 0);
   SIGNAL bi_in : std_logic_vector(15 DOWNTO 0);
   SIGNAL bq_in : std_logic_vector(15 DOWNTO 0);
   SIGNAL aiq_in_sel : std_logic;

   SIGNAL data_req_dis, data_req_dis_o : std_logic;

   --signal data_req_reg           : std_logic;
   --signal data_req_mimo          : std_logic;
   --signal data_req_siso          : std_logic;

   SIGNAL mimo_en : std_logic;

   SIGNAL diq_out_mimo : std_logic_vector(63 DOWNTO 0);
   SIGNAL diq_out_siso : std_logic_vector(63 DOWNTO 0);

   SIGNAL xen_mimo : std_logic;
   SIGNAL xen_siso : std_logic;
   SIGNAL xen : std_logic;
   SIGNAL xen_reg0 : std_logic;
   SIGNAL xen_reg1 : std_logic;
   SIGNAL inst1_xen, inst9_data_valid, inst1_xen_reg0, inst1_xen_reg1 : std_logic;
   SIGNAL yp_ai_reg, yp_aq_reg, yp_ai_prim, yp_aq_prim, yp_bi_prim, yp_bq_prim : std_logic_vector(15 DOWNTO 0);
   signal reset_n2, fix_mimo, dpdtop_en: std_logic;
   signal data_req_reg, data_req_reg2, data_req_int,data_valid_reg: std_logic;
	
	attribute MARK_DEBUG : string;
	attribute MARK_DEBUG of ai_in: signal is "TRUE";
    attribute MARK_DEBUG of aq_in: signal is "TRUE";
    attribute MARK_DEBUG of bi_in: signal is "TRUE";
    attribute MARK_DEBUG of bq_in: signal is "TRUE";
    attribute MARK_DEBUG of xen: signal is "TRUE";

BEGIN
   -- ----------------------------------------------------------------------------
   -- Internal logic 
   -- ----------------------------------------------------------------------------
   -- Data required internal signal

   reset_n_soft<= reset_n2;
	
	data_req_dis_proc : PROCESS (clk, reset_n2)  -- was reset_n 21.4.2020
   BEGIN
      IF reset_n2 = '0' THEN
         data_req_dis <= '0';
      ELSIF (clk'event AND clk = '1') THEN
         IF inst1_xen = '1' THEN
            data_req_dis <= NOT data_req_dis;
         END IF;
      END IF;
   END PROCESS;
   -- To know SISO or MIMO mode
   mimo_en <= ch_en(0) AND ch_en(1);

   -- Select signal for A channels sample MUX
   aiq_in_sel <= '0' WHEN ((mimo_en = '1') and (fix_mimo='0')) ELSE
      data_req_dis;

   -- diq_in bus contains samples in following order: 
   --    MIMO mode:           SISO mode:
   --    AI=diq_in[15:0]      AI(0)=diq_in[15:0]
   --    AQ=diq_in[31:16]     AQ(0)=diq_in[31:16]
   --    BI=diq_in[47:32]     AI(1)=diq_in[47:32]
   --    BQ=diq_in[64:48]     AQ(1)=diq_in[64:48]

   -- In SISO mode A channel samples are muxed from diq_in bus
   ai_in <= diq_in(16 * 1 - 1 DOWNTO 16 * 0) WHEN aiq_in_sel = '0' ELSE
      diq_in(16 * 3 - 1 DOWNTO 16 * 2);
   aq_in <= diq_in(16 * 2 - 1 DOWNTO 16 * 1) WHEN aiq_in_sel = '0' ELSE
      diq_in(16 * 4 - 1 DOWNTO 16 * 3);
   
   bi_in <= diq_in(16 * 3 - 1 DOWNTO 16 * 2) when (fix_mimo='0') else  ai_in;
   bq_in <= diq_in(16 * 4 - 1 DOWNTO 16 * 3) when (fix_mimo='0') else  aq_in;
   -- ----------------------------------------------------------------------------
   -- DPDTop
   -- ----------------------------------------------------------------------------   
   inst0_DPDTop : DPDTop

   GENERIC MAP (
      ADPDCFG_START_ADDR   => g_ADPDCFG_START_ADDR, 
      CFR0CFG_START_ADDR   => g_CFR0CFG_START_ADDR,
      CFR1CFG_START_ADDR   => g_CFR1CFG_START_ADDR, 
      FIR0CFG_START_ADDR   => g_FIR0CFG_START_ADDR,
      FIR1CFG_START_ADDR   => g_FIR1CFG_START_ADDR,
      DPD_enable => DPD_enable
   )
   PORT MAP(
      clk_X2 => clk, -- 122.88 MHz
      reset_n => reset_n,
      mem_reset_n => mem_reset_n,
      from_memcfg => from_memcfg,
      sleep => sleep or (not dpdtop_en),
      ai_in => ai_in,
      aq_in => aq_in,
      bi_in => bi_in,
      bq_in => bq_in,
      sdin => sdin,
      sclk => sclk,
      sen => sen,
      sdout => sdout,
      xp_ai => xp_ai,
      xp_aq => xp_aq,
      xp_bi => xp_bi,
      xp_bq => xp_bq,
      yp_ai => yp_ai_prim,
      yp_aq => yp_aq_prim,
      yp_bi => yp_bi_prim,
      yp_bq => yp_bq_prim,
      xen => inst1_xen, --30.72 MSps
      yen => inst9_data_valid, -- 61.44 MSps		
      cap_en => cap_en,
      cap_cont_en => cap_cont_en,
      cap_size => cap_size,
      cap_resetn => cap_resetn,
      PAEN0 => PAEN0,
      PAEN1 => PAEN1,
      DCEN0 => DCEN0,
      DCEN1 => DCEN1,
      rf_sw => rf_sw,
	  reset_n2=> reset_n2,
      tx_en => tx_en,
      capture_en =>capture_en,
      reset_n_software => reset_n_software,
      lms3_monitoring => lms3_monitoring,
      fix_mimo => fix_mimo,
      dpdtop_en => dpdtop_en
   );

   -- for input
   data_req_dis_o_proc : PROCESS (clk, reset_n2)  -- was reset_n 21.4.2020
   BEGIN
      IF reset_n2 = '0' THEN
         data_req_dis_o <= '0';
      ELSIF (clk'event AND clk = '1') THEN
         IF inst1_xen = '1' THEN
            data_req_dis_o <= NOT data_req_dis_o;
         END IF;
      END IF;
   END PROCESS;

   -- for output
   PROCESS (clk, reset_n)
   BEGIN
      IF reset_n = '0' THEN
         yp_ai_reg <= (OTHERS => '0');
         yp_aq_reg <= (OTHERS => '0');
      ELSIF (clk'event AND clk = '1') THEN
         IF inst9_data_valid = '1' THEN
            yp_ai_reg <= yp_ai_prim;
            yp_aq_reg <= yp_aq_prim;
         END IF;
      END IF;
   END PROCESS;

   yp_ai <= yp_ai_prim;
   yp_aq <= yp_aq_prim;
   yp_bi <= yp_bi_prim;
   yp_bq <= yp_bq_prim;

   --xen_mimo <= inst9_data_valid;
   --xen_siso <= inst9_data_valid AND data_req_dis_o;

   xen_mimo <= inst1_xen;
   xen_siso <= inst1_xen AND data_req_dis_o;
   xen <= xen_mimo WHEN ((mimo_en = '1') and (fix_mimo='0')) ELSE
      xen_siso;

   -- In MIMO mode samples from A and B channels are concatenated into one 64bit bus
   diq_out_mimo <= yp_bq_prim & yp_bi_prim & yp_aq_prim & yp_ai_prim;

   -- In SISO mode only samples from A channel are concatenated into one 64bit bus
   diq_out_siso <= yp_aq_prim & yp_ai_prim & yp_aq_reg & yp_ai_reg;

   -- ----------------------------------------------------------------------------
   -- Output ports
   -- ----------------------------------------------------------------------------
   -- for input
   -- B.J. 17.11.2021
   -- I want to slow down request in MIMO 2X   
   --data_req <= xen;
   
   -- Dummy, when DPD or CFR are not used  
   data_req_proc : process(clk, reset_n)  -- for fclk=61.44
   begin
      if reset_n = '0' then 
         data_req_reg <= '0';  -- 30.72
         data_req_reg2 <= '0';   -- 15.36       
      elsif (clk'event AND clk='1') then
         data_req_reg <= NOT data_req_reg;         
         if (data_req_reg = '1') then
           data_req_reg2 <= NOT data_req_reg2;
         end if;        
      end if;
   end process; 
   
  data_req_int <= data_req_reg when ch_en = "11" else -- 30.72
        (data_req_reg  and  data_req_reg2);    -- 15.36 (every fourth pulse)
  
  data_valid_reg <= data_req_reg; -- 30.72  
  
   
 -----------------------------  
   
   data_req    <= data_req_int when (dpdtop_en='0') else
          xen;

   -- for output
   data_valid  <= data_valid_reg when (dpdtop_en='0') else
          inst9_data_valid;

   --    diq_out bus contains samples in following order: 
   --    MIMO mode:           SISO mode:
   --    AI=diq_out[15:0]      AI(0)=diq_out[15:0]
   --    AQ=diq_out[31:16]     AQ(0)=diq_out[31:16]
   --    BI=diq_out[47:32]     AI(1)=diq_out[47:32]
   --    BQ=diq_out[64:48]     AQ(1)=diq_out[64:48]
   
   diq_out     <= diq_in  when (dpdtop_en='0') else
      diq_out_mimo WHEN (mimo_en = '1') ELSE
      diq_out_siso;
      
 
END arch;