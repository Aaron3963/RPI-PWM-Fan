#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <wiringPi.h>

// 控制风扇的GPIO接口 wiringPi标准
#define FAN_PWM 1
#define FAN_SPE 16
// 启动阈值 高于它则开启风扇
#define STA_TEMP 40
// 低温阈值 低于它则关闭风扇
#define MIN_TEMP 35
// 高温阈值 高于它则全速运转
#define MAX_TEMP 65
// 多长时间读取一次CPU温度 单位毫秒 总循环时间 加上测速时间 速度转换时间
#define SAMPLING 10000
// PWM最低占空比 避免太低启动不了 不能超过占空比最大值
// 转速太低散热效果太低 调高可加强散热效果 可运行test功能测试
#define dc_base 400
// 测速时间 单位毫秒 有2秒加减速时间 总测试时间再加2秒 时间越久越准确
#define TEST_T 4000
// 占空比最大值 根据想要的频率计算
#define Ran_m 950

// 获取cpu温度
int cpu_temp()
{
    int temp;
    FILE *fp_rt;
    fp_rt = fopen("/sys/class/thermal/thermal_zone0/temp", "r");
    fscanf(fp_rt, "%d", &temp);
    fclose(fp_rt);
    return temp;
}

// 获取cpu频率
int cpu_freq()
{
    int freq;
    FILE *fp_rf;
    fp_rf = fopen("/sys/devices/system/cpu/cpu0/cpufreq/scaling_cur_freq", "r");
    fscanf(fp_rf, "%d", &freq);
    fclose(fp_rf);
    return freq;
}

// 获取cpu最高频率
int cpu_freq_max()
{
    int freq;
    FILE *fp_rfm;
    fp_rfm = fopen("/sys/devices/system/cpu/cpu0/cpufreq/scaling_max_freq", "r");
    fscanf(fp_rfm, "%d", &freq);
    fclose(fp_rfm);
    return freq;
}

// 格式化输出当前时间
char timeNow[20];
int time_n()
{
    time_t t = time(0);
    strftime(timeNow, 20, "%Y-%m-%d %H:%M:%S", localtime(&t));
    return 0;
}
// 日志记录
char log_t[80];
int log_w()
{
    FILE *fp_w;
    fp_w = fopen("/var/log/pwmfan.log", "at+");
    fprintf(fp_w, "%s", log_t);
    fclose(fp_w);
    return 0;
}

// gpio初始化
int gpioset()
{
    // 使用wiring编码去初始化GPIO序号
    wiringPiSetup();
    // 设置GPIO电气属性 启动硬件pwm需su权限
    // 硬件pwm 只支持1号脚位
    pinMode(FAN_PWM, PWM_OUTPUT);
    // 对输入引脚上拉电平 PUD_UP
    pinMode(FAN_SPE, INPUT);
    pullUpDnControl(FAN_SPE, PUD_UP);
    // 设置pwm频率
    // 时钟主频19.2MHz pwm脉宽范围: 0~50 分频系数: 16
    // pwm频率24kHz 19.2MHz / 50 / 16 = 24kHz
    // pwmSetRange(Ran_m): 最大空占比, 
	// pwmSetClock(16): 分频系数
    // pwm模式设置 PWM_MODE_MS/PWM_MODE_BAL 默认BAL 需设置为MS
    pwmSetRange(Ran_m);
    pwmSetClock(1);
    pwmSetMode(PWM_MODE_MS);
    return 0;
}

// 启动风扇 开启调速功能
int start_fan(void)
{
    //定义变量
    double temp;
    int freq, freq_max, dc;
    int is_work, i, dc_t;
    // 初始化gpio
    gpioset();
    time_n();
    printf("%s Start Fan Service\n", timeNow);
    sprintf(log_t, "%s Start Fan Service\n", timeNow);
    log_w();

    // pwm硬件输出 pwmWrite(FAN_PWM, dc);

    is_work = 1;
    dc_t = Ran_m;

    // 获取cpu温度 循环调速
    freq_max = cpu_freq_max() / 1000;

    //pwmWrite(FAN_PWM, 960);

    while (1){
        temp = cpu_temp() / 1000.0;
        freq = cpu_freq() / 1000;
        if (is_work == 1){
            // 低于低温阈值 则关闭风扇
            if (temp <= MIN_TEMP){
                dc = 0;
            }
            // 超过高温阈值 则全速运行
            else if (temp >= MAX_TEMP){
                dc = Ran_m;
            }
            // 在低温阈值和高温阈值之间时 根据温度线性使用PWM控制风扇转速
            else{
                dc = dc_base + (temp - MIN_TEMP) / (MAX_TEMP - MIN_TEMP) * (Ran_m - dc_base);
            }
            // for循环 线性变换速度 避免急启急停
            if (dc > 0){
                if (dc < dc_t){
                    for (i = dc_t - 1; i >= dc; i-=1){
                        pwmWrite(FAN_PWM, i);
                        //delay(400);
                        delay(10);
                    }
                }
                else{
                    for (i = dc_t + 1; i <= dc; i+=1){
                        pwmWrite(FAN_PWM, i);
                        //delay(400);
                        delay(10);
                    }
                }
                is_work = 1;
                dc_t = dc;

                time_n();
                printf("%s Temp: %2.1f°C CPU:%4d/%4dMhz PWM:%4d Fan Power:%3d%%\n", 
                timeNow, temp, freq, freq_max, dc, (dc - dc_base) * 100 / (Ran_m - dc_base));
                sprintf(log_t, "%s Temp: %2.1f°C CPU:%4d/%4dMhz PWM:%4d Fan Power:%3d%%\n", 
                timeNow, temp, freq, freq_max, dc, (dc - dc_base) * 100 / (Ran_m - dc_base));
            }
            else{
                if (dc_t > 0){
                    for (i = dc_t; i >= 0; i-=1){
                        pwmWrite(FAN_PWM, i);
                        delay(10);
                    }
                }
                is_work = 0;
                dc_t = 0;
                time_n();
                printf("%s Temp: %2.1f°C Freq:%4d/%4dMhz Stop Running\n", timeNow, temp, freq, freq_max);
                sprintf(log_t, "%s Temp: %2.1f°C Freq:%4d/%4dMhz Stop Running\n", timeNow, temp, freq, freq_max);
            }
            log_w();
        }
        else{
            // 温度低于启动温度则一直停止
            if (temp >= STA_TEMP){
                is_work = 1;
            }
            delay(SAMPLING);
        }
        // 接口守护 防止接口被其它程序调用而停止
        if (dc != 0){
			gpioset();
         }
        //设置采样频率
        delay(SAMPLING);
    }

    return 0;
}

// 停止进程 停止风扇
int stop_fan()
{
    // 杀死进程 需su权限 修改本文件名需修改
    system("sudo kill `ps -ef | grep '[p]wmfan start' | awk '{ print $2 }'` >/dev/null 2>&1");
    gpioset();
    pwmWrite(FAN_PWM, 0);
    time_n();
    printf("%s Stop Fan Service\n", timeNow);
    sprintf(log_t, "%s Stop Fan Service\n", timeNow);
    log_w();
    return 0;
}

// // 风扇转速测试 不同空占比下的速度测试
// // 先杀死风扇服务进程 避免干扰 测试完成需手动启动风扇服务
void test() {
  int i;
  stop_fan(); gpioset();
  printf("Fan Speed in Different PWM DutyCycle\nPWM DutyCycle 0 => %d  Time Needed: %ds\n", Ran_m, Ran_m*(TEST_T/1000+2));
  for (i = 0; i <= Ran_m; i+=50) {
    pwmWrite(FAN_PWM, i);
    time_n();
    printf("%s Pwm: %2d\n",timeNow, i);
    delay(5000);
  }
  delay(1000);
  printf("PWM DutyCycle %d => 0  Time Needed: %ds\n", Ran_m, Ran_m*(TEST_T/1000+2));
  for (i = Ran_m; i >= 0; i-=1) {
    pwmWrite(FAN_PWM, i);
    time_n();
    printf("%s Pwm: %2d\n",timeNow, i);
  }
  printf("Test Finished Please Start Pwmfan Service\n");
}

// 主函数
int main(int argc, char **argv)
{
    if (argc <= 1)
    {
        printf("Usage: sudo %s [start|stop|restart|test|help|version]\n", argv[0]);
    }
    else
    {
        if (strcmp(argv[1], "start") == 0)
        {
            start_fan();
        }
        else if (strcmp(argv[1], "stop") == 0)
        {
            stop_fan();
        }
        else if (strcmp(argv[1], "test") == 0 ) { 
            test(); 
        }
        else if (strcmp(argv[1], "restart") == 0)
        {
            stop_fan();
            start_fan();
        }
        else if (strcmp(argv[1], "help") == 0)
        {
            printf("使用方法: %s [start|stop|restart|test|help|version]\n", argv[0]);
            printf("   start: 启动风扇服务\n");
            printf("    stop: 停止风扇服务\n");
            printf(" restart: 重启风扇服务\n");
            printf("    test: 测试不同占空下风扇速度\n");
            printf("    help: 显示帮助\n");
            printf(" version: 显示版本\n");
        }
        else if (strcmp(argv[1], "version") == 0)
        {
            printf("PWM Fan Service\nVersion: 1.1.0\nBy: ....\nHave a Nise Day\n");
        }
        else
        {
            printf("Usage: sudo %s [start|stop|restart|test|help|version]\n", argv[0]);
        }
    }
    return 0;
}