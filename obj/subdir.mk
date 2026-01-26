################################################################################
# MRS Version: 2.3.0
# Automatically-generated file. Do not edit!
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
C_SRCS += \
../main.c \
../nrf24_simple.c 

C_DEPS += \
./main.d \
./nrf24_simple.d 

OBJS += \
./main.o \
./nrf24_simple.o 

DIR_OBJS += \
././*.o \

DIR_DEPS += \
././*.d \

DIR_EXPANDS += \
././*.234r.expand \


# Each subdirectory must supply rules for building sources it contributes
%.o: ../%.c
	@	riscv-none-embed-gcc -march=rv32ecxw -mabi=ilp32e -msmall-data-limit=0 -msave-restore -fmax-errors=20 -Os -fmessage-length=0 -fsigned-char -ffunction-sections -fdata-sections -fno-common -Wunused -Wuninitialized -g -I"c:/Users/shaik/mounriver-studio-projects/CH32V003F4P6_tank_sensor/Debug" -I"c:/Users/shaik/mounriver-studio-projects/CH32V003F4P6_tank_sensor/Core" -I"c:/Users/shaik/mounriver-studio-projects/CH32V003F4P6_tank_sensor/User" -I"c:/Users/shaik/mounriver-studio-projects/CH32V003F4P6_tank_sensor/Peripheral/inc" -std=gnu99 -MMD -MP -MF"$(@:%.o=%.d)" -MT"$(@)" -c -o "$@" "$<"

