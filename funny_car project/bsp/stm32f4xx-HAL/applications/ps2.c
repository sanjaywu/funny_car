#include "ps2.h"

#define DBG_SECTION_NAME            "ps2"
#define DBG_LEVEL                   DBG_LOG
#include <rtdbg.h>

#define THREAD_DELAY_TIME           10
#define THREAD_PRIORITY             15
#define THREAD_STACK_SIZE           1024
#define THREAD_TIMESLICE            5

#define KEEP_TIME()                 _delay_us(8);

static rt_base_t        ps2_cs_pin; 
static rt_base_t        ps2_clk_pin;
static rt_base_t        ps2_do_pin; 
static rt_base_t        ps2_di_pin;

static uint8_t          light_mode   =   PS2_NO_MODE;
static rt_thread_t      tid_ps2      =   RT_NULL;
static ps2_keys_data_t  ps2_keys     =  {0};
static ps2_ctrl_data_t  ctrl_data    =  {0};


static void hal_cs_high(void)
{
    rt_pin_write(ps2_cs_pin, PIN_HIGH);
}
static void hal_cs_low(void)
{
    rt_pin_write(ps2_cs_pin, PIN_LOW);
}
static void hal_clk_high(void)
{
    rt_pin_write(ps2_clk_pin, PIN_HIGH);
}
static void hal_clk_low(void)
{
    rt_pin_write(ps2_clk_pin, PIN_LOW);
}
static void hal_do_high(void)
{
    rt_pin_write(ps2_do_pin, PIN_HIGH);
}
static void hal_do_low(void)
{
    rt_pin_write(ps2_do_pin, PIN_LOW);
}
static int hal_read_di(void)
{
    return rt_pin_read(ps2_di_pin);
}

static void _delay_us(uint16_t us)
{
    for (int i = 0; i < us; i++)
    {
        for (int j = 0; j < 0x1F;)
            j++;
    }
}

static uint8_t _transfer(uint8_t data)
{
    uint8_t temp = 0;

    for (uint16_t i = 0x01; i < 0x0100; i <<= 1)
    {
        if (i & data)
            hal_do_high(); 
        else
            hal_do_low();

        hal_clk_high();
        KEEP_TIME();
        hal_clk_low();
        if (hal_read_di())
            temp = i | temp;
        KEEP_TIME();
        hal_clk_high();
    }
    
    return temp;
}

static void transfer(const uint8_t *pb_send, uint8_t *pb_recv, uint8_t len)
{
    hal_cs_low();
    KEEP_TIME();
    for (uint8_t i = 0; i < len; i++)
    {
        pb_recv[i] = _transfer(pb_send[i]);
    }
    hal_cs_high();
    KEEP_TIME();
}

int ps2_scan(ps2_ctrl_data_t *pt)
{
    uint8_t temp[9] = {0};

    temp[0] = 0x01;
    temp[1] = 0x42;
    temp[3] = 0;
    temp[4] = 0;

    transfer(temp, temp, 9);
    
    pt->button = temp[3] | (temp[4] << 8);
    pt->right_stick_x = temp[5];
    pt->right_stick_y = temp[6];
    pt->left_stick_x = temp[7];
    pt->left_stick_y = temp[8];

    if (temp[2] == 0x5A)
    {
        light_mode = temp[1];
        return 1;
    }
    else
    {
        light_mode = PS2_NO_MODE;
    }

    return 0;
}

/**
 * @return PS2_GREEN_MDOE or PS2_RED_MDOE or other(no connect) 
 */
uint8_t ps2_get_mode(void)
{
    return light_mode;
}

static rt_err_t ps2_get_keys(ps2_keys_data_t *keys)
{
    if (RT_NULL == keys)
        return -RT_EINVAL;
    
    ps2_scan(&ctrl_data);

    for (int i = 0; i < 16; i++)
    {
        if (ctrl_data.button & (0x01 << i))
        {
            keys->key_value[i] = 1;
        }
        else
        {
            keys->key_value[i] = 0;
        }
    }
    keys->key_value[16] = ctrl_data.left_stick_x;
    keys->key_value[17] = ctrl_data.left_stick_y;
    keys->key_value[18] = ctrl_data.right_stick_x;
    keys->key_value[19] = ctrl_data.right_stick_y;

    return RT_EOK;
}

ps2_keys_data_t tmp_keys = {0};

extern float rotation;
extern float roll_target;
static void ps2_thread_entry(void *param)
{
		static uint8_t ly_rocker_offset = 0;//摇杆模拟量中间值
		static uint8_t rx_rocker_offset = 0;
		while(1)
		{
				ps2_get_keys(&tmp_keys);
				if(ps2_get_mode() == PS2_RED_MODE)
				{
						ly_rocker_offset = tmp_keys.key_value[PS2_KEY_ROCKER_LY];
						rx_rocker_offset = tmp_keys.key_value[PS2_KEY_ROCKER_RX];
						break;
				}
				rt_thread_mdelay(THREAD_DELAY_TIME);   
		}
		float roll_target_tmp;
    while (1)
    {
        ps2_get_keys(&tmp_keys);
			
        if (ps2_get_mode() == PS2_RED_MODE)// &&(0 != rt_memcmp(&ps2_keys, &tmp_keys, sizeof(ps2_keys_data_t))))
        {	
						roll_target_tmp = (tmp_keys.key_value[PS2_KEY_ROCKER_LY] - ly_rocker_offset) * 10 / 255.0;// 正负5度变化
						rotation = (tmp_keys.key_value[PS2_KEY_ROCKER_RX] - rx_rocker_offset) * 12; //作为转速，直接加在速度内环
        }
				else
				{
						roll_target = 0;
						rotation = 0;
				}
				if(roll_target_tmp > roll_target)
					roll_target += 0.1f;
				else
					roll_target -= 0.1f;
        rt_thread_mdelay(THREAD_DELAY_TIME);   
    }
}

/* 获取当前键值 */
ps2_keys_data_t ps2_get_current_keys(void)
{
    return ps2_keys;
}

/* 获取当前键值 */
ps2_ctrl_data_t ps2_get_current_ctrl(void)
{
    return ctrl_data;
}


int ps2_init(void)
{
    ps2_cs_pin  = PS2_CS_PIN;
    ps2_clk_pin = PS2_SCK_PIN;
    ps2_do_pin  = PS2_DO_PIN;
    ps2_di_pin  = PS2_DI_PIN;

    rt_pin_mode(ps2_cs_pin,  PIN_MODE_OUTPUT);
    rt_pin_mode(ps2_clk_pin, PIN_MODE_OUTPUT);
    rt_pin_mode(ps2_do_pin,  PIN_MODE_OUTPUT);
    rt_pin_mode(ps2_di_pin,  PIN_MODE_INPUT);
    
    hal_cs_high();
    hal_clk_high();

    tid_ps2 = rt_thread_create("ps2",
                          ps2_thread_entry, RT_NULL,
                          THREAD_STACK_SIZE,
                          THREAD_PRIORITY, THREAD_TIMESLICE);

    if (tid_ps2 != RT_NULL)
    {
        rt_thread_startup(tid_ps2);
    }
    else
    {
        LOG_E("Can't create thread for ps2");
        return -RT_ENOSYS;
    }
    
    return RT_EOK;
}

//INIT_DEVICE_EXPORT(ps2_init);
