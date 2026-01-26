################################################################################
# MRS Version: 2.3.0
# Automatically-generated file. Do not edit!
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
C_SRCS += \
../User/main.c \
../User/nrf24_simple.c \
../User/system_ch32v00x.c 

C_DEPS += \
./User/main.d \
./User/nrf24_simple.d \
./User/system_ch32v00x.d 

OBJS += \
./User/main.o \
./User/nrf24_simple.o \
./User/system_ch32v00x.o 

DIR_OBJS += \
./User/*.o \

DIR_DEPS += \
./User/*.d \

DIR_EXPANDS += \
./User/*.234r.expand \


# Each subdirectory must supply rules for building sources it contributes
User/%.o: ../User/%.c
	@	riscv-none-embed-gcc -march=rv32ecxw -mabi=ilp32e -msmall-data-limit=0 -msave-restore -fmax-errors=20 -Os -fmessage-length=0 -fsigned-char -ffunction-sections -fdata-sections -fno-common -Wunused -Wuninitialized -g -I"c:/Users/shaik/mounriver-studio-projects/CH32V003F4P6_tank_sensor/Debug" -I"c:/Users/shaik/mounriver-studio-projects/CH32V003F4P6_tank_sensor/Core" -I"c:/Users/shaik/mounriver-studio-projects/CH32V003F4P6_tank_sensor/User" -I"c:/Users/shaik/mounriver-studio-projects/CH32V003F4P6_tank_sensor/Peripheral/inc" -std=gnu99 -MMD -MP -MF"$(@:%.o=%.d)" -MT"$(@)" -c -o "$@" "$<"

