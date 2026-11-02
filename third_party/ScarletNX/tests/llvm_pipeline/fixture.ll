; ModuleID = '/mnt/data/llvm_78910/bench_small.c'
source_filename = "/mnt/data/llvm_78910/bench_small.c"
target datalayout = "e-m:e-p270:32:32-p271:32:32-p272:64:64-i64:64-i128:128-f80:128-n8:16:32:64-S128"
target triple = "x86_64-unknown-linux-gnu"

%struct.CPU = type { [32 x i64], i64 }

; Function Attrs: nounwind
define dso_local i64 @F0(ptr noundef %0, i64 noundef %1) #0 {
  %3 = alloca ptr, align 8
  %4 = alloca i64, align 8
  %5 = alloca i64, align 8
  store ptr %0, ptr %3, align 8
  store i64 %1, ptr %4, align 8
  %6 = load ptr, ptr %3, align 8
  %7 = getelementptr inbounds %struct.CPU, ptr %6, i32 0, i32 0
  %8 = load i64, ptr %4, align 8
  %9 = lshr i64 %8, 2
  %10 = and i64 %9, 31
  %11 = getelementptr inbounds [32 x i64], ptr %7, i64 0, i64 %10
  %12 = load i64, ptr %11, align 8
  %13 = load i64, ptr %4, align 8
  %14 = add i64 %12, %13
  store i64 %14, ptr %5, align 8
  %15 = load i64, ptr %5, align 8
  %16 = mul i64 %15, 37
  %17 = load i64, ptr %5, align 8
  %18 = lshr i64 %17, 7
  %19 = xor i64 %16, %18
  store i64 %19, ptr %5, align 8
  %20 = load i64, ptr %5, align 8
  %21 = load ptr, ptr %3, align 8
  %22 = getelementptr inbounds %struct.CPU, ptr %21, i32 0, i32 0
  %23 = load i64, ptr %4, align 8
  %24 = lshr i64 %23, 2
  %25 = and i64 %24, 31
  %26 = getelementptr inbounds [32 x i64], ptr %22, i64 0, i64 %25
  store i64 %20, ptr %26, align 8
  %27 = load i64, ptr %5, align 8
  ret i64 %27
}

; Function Attrs: nounwind
define dso_local i64 @F1(ptr noundef %0, i64 noundef %1) #0 {
  %3 = alloca ptr, align 8
  %4 = alloca i64, align 8
  %5 = alloca i64, align 8
  store ptr %0, ptr %3, align 8
  store i64 %1, ptr %4, align 8
  %6 = load ptr, ptr %3, align 8
  %7 = getelementptr inbounds %struct.CPU, ptr %6, i32 0, i32 0
  %8 = load i64, ptr %4, align 8
  %9 = lshr i64 %8, 2
  %10 = and i64 %9, 31
  %11 = getelementptr inbounds [32 x i64], ptr %7, i64 0, i64 %10
  %12 = load i64, ptr %11, align 8
  %13 = load i64, ptr %4, align 8
  %14 = add i64 %12, %13
  store i64 %14, ptr %5, align 8
  %15 = load i64, ptr %5, align 8
  %16 = mul i64 %15, 37
  %17 = load i64, ptr %5, align 8
  %18 = lshr i64 %17, 7
  %19 = xor i64 %16, %18
  store i64 %19, ptr %5, align 8
  %20 = load i64, ptr %5, align 8
  %21 = load ptr, ptr %3, align 8
  %22 = getelementptr inbounds %struct.CPU, ptr %21, i32 0, i32 0
  %23 = load i64, ptr %4, align 8
  %24 = lshr i64 %23, 2
  %25 = and i64 %24, 31
  %26 = getelementptr inbounds [32 x i64], ptr %22, i64 0, i64 %25
  store i64 %20, ptr %26, align 8
  %27 = load ptr, ptr %3, align 8
  %28 = load i64, ptr %4, align 8
  %29 = add i64 %28, 4
  %30 = call i64 @F0(ptr noundef %27, i64 noundef %29)
  %31 = load i64, ptr %5, align 8
  %32 = add i64 %30, %31
  ret i64 %32
}

; Function Attrs: nounwind
define dso_local i64 @F2(ptr noundef %0, i64 noundef %1) #0 {
  %3 = alloca ptr, align 8
  %4 = alloca i64, align 8
  %5 = alloca i64, align 8
  store ptr %0, ptr %3, align 8
  store i64 %1, ptr %4, align 8
  %6 = load ptr, ptr %3, align 8
  %7 = getelementptr inbounds %struct.CPU, ptr %6, i32 0, i32 0
  %8 = load i64, ptr %4, align 8
  %9 = lshr i64 %8, 2
  %10 = and i64 %9, 31
  %11 = getelementptr inbounds [32 x i64], ptr %7, i64 0, i64 %10
  %12 = load i64, ptr %11, align 8
  %13 = load i64, ptr %4, align 8
  %14 = add i64 %12, %13
  store i64 %14, ptr %5, align 8
  %15 = load i64, ptr %5, align 8
  %16 = mul i64 %15, 37
  %17 = load i64, ptr %5, align 8
  %18 = lshr i64 %17, 7
  %19 = xor i64 %16, %18
  store i64 %19, ptr %5, align 8
  %20 = load i64, ptr %5, align 8
  %21 = load ptr, ptr %3, align 8
  %22 = getelementptr inbounds %struct.CPU, ptr %21, i32 0, i32 0
  %23 = load i64, ptr %4, align 8
  %24 = lshr i64 %23, 2
  %25 = and i64 %24, 31
  %26 = getelementptr inbounds [32 x i64], ptr %22, i64 0, i64 %25
  store i64 %20, ptr %26, align 8
  %27 = load ptr, ptr %3, align 8
  %28 = load i64, ptr %4, align 8
  %29 = add i64 %28, 4
  %30 = call i64 @F1(ptr noundef %27, i64 noundef %29)
  %31 = load i64, ptr %5, align 8
  %32 = add i64 %30, %31
  ret i64 %32
}

; Function Attrs: nounwind
define dso_local i64 @F3(ptr noundef %0, i64 noundef %1) #0 {
  %3 = alloca ptr, align 8
  %4 = alloca i64, align 8
  %5 = alloca i64, align 8
  store ptr %0, ptr %3, align 8
  store i64 %1, ptr %4, align 8
  %6 = load ptr, ptr %3, align 8
  %7 = getelementptr inbounds %struct.CPU, ptr %6, i32 0, i32 0
  %8 = load i64, ptr %4, align 8
  %9 = lshr i64 %8, 2
  %10 = and i64 %9, 31
  %11 = getelementptr inbounds [32 x i64], ptr %7, i64 0, i64 %10
  %12 = load i64, ptr %11, align 8
  %13 = load i64, ptr %4, align 8
  %14 = add i64 %12, %13
  store i64 %14, ptr %5, align 8
  %15 = load i64, ptr %5, align 8
  %16 = mul i64 %15, 37
  %17 = load i64, ptr %5, align 8
  %18 = lshr i64 %17, 7
  %19 = xor i64 %16, %18
  store i64 %19, ptr %5, align 8
  %20 = load i64, ptr %5, align 8
  %21 = load ptr, ptr %3, align 8
  %22 = getelementptr inbounds %struct.CPU, ptr %21, i32 0, i32 0
  %23 = load i64, ptr %4, align 8
  %24 = lshr i64 %23, 2
  %25 = and i64 %24, 31
  %26 = getelementptr inbounds [32 x i64], ptr %22, i64 0, i64 %25
  store i64 %20, ptr %26, align 8
  %27 = load ptr, ptr %3, align 8
  %28 = load i64, ptr %4, align 8
  %29 = add i64 %28, 4
  %30 = call i64 @F2(ptr noundef %27, i64 noundef %29)
  %31 = load i64, ptr %5, align 8
  %32 = add i64 %30, %31
  ret i64 %32
}

; Function Attrs: nounwind
define dso_local i64 @F4(ptr noundef %0, i64 noundef %1) #0 {
  %3 = alloca ptr, align 8
  %4 = alloca i64, align 8
  %5 = alloca i64, align 8
  store ptr %0, ptr %3, align 8
  store i64 %1, ptr %4, align 8
  %6 = load ptr, ptr %3, align 8
  %7 = getelementptr inbounds %struct.CPU, ptr %6, i32 0, i32 0
  %8 = load i64, ptr %4, align 8
  %9 = lshr i64 %8, 2
  %10 = and i64 %9, 31
  %11 = getelementptr inbounds [32 x i64], ptr %7, i64 0, i64 %10
  %12 = load i64, ptr %11, align 8
  %13 = load i64, ptr %4, align 8
  %14 = add i64 %12, %13
  store i64 %14, ptr %5, align 8
  %15 = load i64, ptr %5, align 8
  %16 = mul i64 %15, 37
  %17 = load i64, ptr %5, align 8
  %18 = lshr i64 %17, 7
  %19 = xor i64 %16, %18
  store i64 %19, ptr %5, align 8
  %20 = load i64, ptr %5, align 8
  %21 = load ptr, ptr %3, align 8
  %22 = getelementptr inbounds %struct.CPU, ptr %21, i32 0, i32 0
  %23 = load i64, ptr %4, align 8
  %24 = lshr i64 %23, 2
  %25 = and i64 %24, 31
  %26 = getelementptr inbounds [32 x i64], ptr %22, i64 0, i64 %25
  store i64 %20, ptr %26, align 8
  %27 = load ptr, ptr %3, align 8
  %28 = load i64, ptr %4, align 8
  %29 = add i64 %28, 4
  %30 = call i64 @F3(ptr noundef %27, i64 noundef %29)
  %31 = load i64, ptr %5, align 8
  %32 = add i64 %30, %31
  ret i64 %32
}

; Function Attrs: nounwind
define dso_local i64 @F5(ptr noundef %0, i64 noundef %1) #0 {
  %3 = alloca ptr, align 8
  %4 = alloca i64, align 8
  %5 = alloca i64, align 8
  store ptr %0, ptr %3, align 8
  store i64 %1, ptr %4, align 8
  %6 = load ptr, ptr %3, align 8
  %7 = getelementptr inbounds %struct.CPU, ptr %6, i32 0, i32 0
  %8 = load i64, ptr %4, align 8
  %9 = lshr i64 %8, 2
  %10 = and i64 %9, 31
  %11 = getelementptr inbounds [32 x i64], ptr %7, i64 0, i64 %10
  %12 = load i64, ptr %11, align 8
  %13 = load i64, ptr %4, align 8
  %14 = add i64 %12, %13
  store i64 %14, ptr %5, align 8
  %15 = load i64, ptr %5, align 8
  %16 = mul i64 %15, 37
  %17 = load i64, ptr %5, align 8
  %18 = lshr i64 %17, 7
  %19 = xor i64 %16, %18
  store i64 %19, ptr %5, align 8
  %20 = load i64, ptr %5, align 8
  %21 = load ptr, ptr %3, align 8
  %22 = getelementptr inbounds %struct.CPU, ptr %21, i32 0, i32 0
  %23 = load i64, ptr %4, align 8
  %24 = lshr i64 %23, 2
  %25 = and i64 %24, 31
  %26 = getelementptr inbounds [32 x i64], ptr %22, i64 0, i64 %25
  store i64 %20, ptr %26, align 8
  %27 = load ptr, ptr %3, align 8
  %28 = load i64, ptr %4, align 8
  %29 = add i64 %28, 4
  %30 = call i64 @F4(ptr noundef %27, i64 noundef %29)
  %31 = load i64, ptr %5, align 8
  %32 = add i64 %30, %31
  ret i64 %32
}

; Function Attrs: nounwind
define dso_local i64 @F6(ptr noundef %0, i64 noundef %1) #0 {
  %3 = alloca ptr, align 8
  %4 = alloca i64, align 8
  %5 = alloca i64, align 8
  store ptr %0, ptr %3, align 8
  store i64 %1, ptr %4, align 8
  %6 = load ptr, ptr %3, align 8
  %7 = getelementptr inbounds %struct.CPU, ptr %6, i32 0, i32 0
  %8 = load i64, ptr %4, align 8
  %9 = lshr i64 %8, 2
  %10 = and i64 %9, 31
  %11 = getelementptr inbounds [32 x i64], ptr %7, i64 0, i64 %10
  %12 = load i64, ptr %11, align 8
  %13 = load i64, ptr %4, align 8
  %14 = add i64 %12, %13
  store i64 %14, ptr %5, align 8
  %15 = load i64, ptr %5, align 8
  %16 = mul i64 %15, 37
  %17 = load i64, ptr %5, align 8
  %18 = lshr i64 %17, 7
  %19 = xor i64 %16, %18
  store i64 %19, ptr %5, align 8
  %20 = load i64, ptr %5, align 8
  %21 = load ptr, ptr %3, align 8
  %22 = getelementptr inbounds %struct.CPU, ptr %21, i32 0, i32 0
  %23 = load i64, ptr %4, align 8
  %24 = lshr i64 %23, 2
  %25 = and i64 %24, 31
  %26 = getelementptr inbounds [32 x i64], ptr %22, i64 0, i64 %25
  store i64 %20, ptr %26, align 8
  %27 = load ptr, ptr %3, align 8
  %28 = load i64, ptr %4, align 8
  %29 = add i64 %28, 4
  %30 = call i64 @F5(ptr noundef %27, i64 noundef %29)
  %31 = load i64, ptr %5, align 8
  %32 = add i64 %30, %31
  ret i64 %32
}

; Function Attrs: nounwind
define dso_local i64 @F7(ptr noundef %0, i64 noundef %1) #0 {
  %3 = alloca ptr, align 8
  %4 = alloca i64, align 8
  %5 = alloca i64, align 8
  store ptr %0, ptr %3, align 8
  store i64 %1, ptr %4, align 8
  %6 = load ptr, ptr %3, align 8
  %7 = getelementptr inbounds %struct.CPU, ptr %6, i32 0, i32 0
  %8 = load i64, ptr %4, align 8
  %9 = lshr i64 %8, 2
  %10 = and i64 %9, 31
  %11 = getelementptr inbounds [32 x i64], ptr %7, i64 0, i64 %10
  %12 = load i64, ptr %11, align 8
  %13 = load i64, ptr %4, align 8
  %14 = add i64 %12, %13
  store i64 %14, ptr %5, align 8
  %15 = load i64, ptr %5, align 8
  %16 = mul i64 %15, 37
  %17 = load i64, ptr %5, align 8
  %18 = lshr i64 %17, 7
  %19 = xor i64 %16, %18
  store i64 %19, ptr %5, align 8
  %20 = load i64, ptr %5, align 8
  %21 = load ptr, ptr %3, align 8
  %22 = getelementptr inbounds %struct.CPU, ptr %21, i32 0, i32 0
  %23 = load i64, ptr %4, align 8
  %24 = lshr i64 %23, 2
  %25 = and i64 %24, 31
  %26 = getelementptr inbounds [32 x i64], ptr %22, i64 0, i64 %25
  store i64 %20, ptr %26, align 8
  %27 = load ptr, ptr %3, align 8
  %28 = load i64, ptr %4, align 8
  %29 = add i64 %28, 4
  %30 = call i64 @F6(ptr noundef %27, i64 noundef %29)
  %31 = load i64, ptr %5, align 8
  %32 = add i64 %30, %31
  ret i64 %32
}

; Function Attrs: nounwind
define dso_local i64 @F8(ptr noundef %0, i64 noundef %1) #0 {
  %3 = alloca ptr, align 8
  %4 = alloca i64, align 8
  %5 = alloca i64, align 8
  store ptr %0, ptr %3, align 8
  store i64 %1, ptr %4, align 8
  %6 = load ptr, ptr %3, align 8
  %7 = getelementptr inbounds %struct.CPU, ptr %6, i32 0, i32 0
  %8 = load i64, ptr %4, align 8
  %9 = lshr i64 %8, 2
  %10 = and i64 %9, 31
  %11 = getelementptr inbounds [32 x i64], ptr %7, i64 0, i64 %10
  %12 = load i64, ptr %11, align 8
  %13 = load i64, ptr %4, align 8
  %14 = add i64 %12, %13
  store i64 %14, ptr %5, align 8
  %15 = load i64, ptr %5, align 8
  %16 = mul i64 %15, 37
  %17 = load i64, ptr %5, align 8
  %18 = lshr i64 %17, 7
  %19 = xor i64 %16, %18
  store i64 %19, ptr %5, align 8
  %20 = load i64, ptr %5, align 8
  %21 = load ptr, ptr %3, align 8
  %22 = getelementptr inbounds %struct.CPU, ptr %21, i32 0, i32 0
  %23 = load i64, ptr %4, align 8
  %24 = lshr i64 %23, 2
  %25 = and i64 %24, 31
  %26 = getelementptr inbounds [32 x i64], ptr %22, i64 0, i64 %25
  store i64 %20, ptr %26, align 8
  %27 = load ptr, ptr %3, align 8
  %28 = load i64, ptr %4, align 8
  %29 = add i64 %28, 4
  %30 = call i64 @F7(ptr noundef %27, i64 noundef %29)
  %31 = load i64, ptr %5, align 8
  %32 = add i64 %30, %31
  ret i64 %32
}

; Function Attrs: nounwind
define dso_local i64 @F9(ptr noundef %0, i64 noundef %1) #0 {
  %3 = alloca ptr, align 8
  %4 = alloca i64, align 8
  %5 = alloca i64, align 8
  store ptr %0, ptr %3, align 8
  store i64 %1, ptr %4, align 8
  %6 = load ptr, ptr %3, align 8
  %7 = getelementptr inbounds %struct.CPU, ptr %6, i32 0, i32 0
  %8 = load i64, ptr %4, align 8
  %9 = lshr i64 %8, 2
  %10 = and i64 %9, 31
  %11 = getelementptr inbounds [32 x i64], ptr %7, i64 0, i64 %10
  %12 = load i64, ptr %11, align 8
  %13 = load i64, ptr %4, align 8
  %14 = add i64 %12, %13
  store i64 %14, ptr %5, align 8
  %15 = load i64, ptr %5, align 8
  %16 = mul i64 %15, 37
  %17 = load i64, ptr %5, align 8
  %18 = lshr i64 %17, 7
  %19 = xor i64 %16, %18
  store i64 %19, ptr %5, align 8
  %20 = load i64, ptr %5, align 8
  %21 = load ptr, ptr %3, align 8
  %22 = getelementptr inbounds %struct.CPU, ptr %21, i32 0, i32 0
  %23 = load i64, ptr %4, align 8
  %24 = lshr i64 %23, 2
  %25 = and i64 %24, 31
  %26 = getelementptr inbounds [32 x i64], ptr %22, i64 0, i64 %25
  store i64 %20, ptr %26, align 8
  %27 = load ptr, ptr %3, align 8
  %28 = load i64, ptr %4, align 8
  %29 = add i64 %28, 4
  %30 = call i64 @F8(ptr noundef %27, i64 noundef %29)
  %31 = load i64, ptr %5, align 8
  %32 = add i64 %30, %31
  ret i64 %32
}

; Function Attrs: nounwind
define dso_local i64 @F10(ptr noundef %0, i64 noundef %1) #0 {
  %3 = alloca ptr, align 8
  %4 = alloca i64, align 8
  %5 = alloca i64, align 8
  store ptr %0, ptr %3, align 8
  store i64 %1, ptr %4, align 8
  %6 = load ptr, ptr %3, align 8
  %7 = getelementptr inbounds %struct.CPU, ptr %6, i32 0, i32 0
  %8 = load i64, ptr %4, align 8
  %9 = lshr i64 %8, 2
  %10 = and i64 %9, 31
  %11 = getelementptr inbounds [32 x i64], ptr %7, i64 0, i64 %10
  %12 = load i64, ptr %11, align 8
  %13 = load i64, ptr %4, align 8
  %14 = add i64 %12, %13
  store i64 %14, ptr %5, align 8
  %15 = load i64, ptr %5, align 8
  %16 = mul i64 %15, 37
  %17 = load i64, ptr %5, align 8
  %18 = lshr i64 %17, 7
  %19 = xor i64 %16, %18
  store i64 %19, ptr %5, align 8
  %20 = load i64, ptr %5, align 8
  %21 = load ptr, ptr %3, align 8
  %22 = getelementptr inbounds %struct.CPU, ptr %21, i32 0, i32 0
  %23 = load i64, ptr %4, align 8
  %24 = lshr i64 %23, 2
  %25 = and i64 %24, 31
  %26 = getelementptr inbounds [32 x i64], ptr %22, i64 0, i64 %25
  store i64 %20, ptr %26, align 8
  %27 = load ptr, ptr %3, align 8
  %28 = load i64, ptr %4, align 8
  %29 = add i64 %28, 4
  %30 = call i64 @F9(ptr noundef %27, i64 noundef %29)
  %31 = load i64, ptr %5, align 8
  %32 = add i64 %30, %31
  ret i64 %32
}

; Function Attrs: nounwind
define dso_local i64 @F11(ptr noundef %0, i64 noundef %1) #0 {
  %3 = alloca ptr, align 8
  %4 = alloca i64, align 8
  %5 = alloca i64, align 8
  store ptr %0, ptr %3, align 8
  store i64 %1, ptr %4, align 8
  %6 = load ptr, ptr %3, align 8
  %7 = getelementptr inbounds %struct.CPU, ptr %6, i32 0, i32 0
  %8 = load i64, ptr %4, align 8
  %9 = lshr i64 %8, 2
  %10 = and i64 %9, 31
  %11 = getelementptr inbounds [32 x i64], ptr %7, i64 0, i64 %10
  %12 = load i64, ptr %11, align 8
  %13 = load i64, ptr %4, align 8
  %14 = add i64 %12, %13
  store i64 %14, ptr %5, align 8
  %15 = load i64, ptr %5, align 8
  %16 = mul i64 %15, 37
  %17 = load i64, ptr %5, align 8
  %18 = lshr i64 %17, 7
  %19 = xor i64 %16, %18
  store i64 %19, ptr %5, align 8
  %20 = load i64, ptr %5, align 8
  %21 = load ptr, ptr %3, align 8
  %22 = getelementptr inbounds %struct.CPU, ptr %21, i32 0, i32 0
  %23 = load i64, ptr %4, align 8
  %24 = lshr i64 %23, 2
  %25 = and i64 %24, 31
  %26 = getelementptr inbounds [32 x i64], ptr %22, i64 0, i64 %25
  store i64 %20, ptr %26, align 8
  %27 = load ptr, ptr %3, align 8
  %28 = load i64, ptr %4, align 8
  %29 = add i64 %28, 4
  %30 = call i64 @F10(ptr noundef %27, i64 noundef %29)
  %31 = load i64, ptr %5, align 8
  %32 = add i64 %30, %31
  ret i64 %32
}

; Function Attrs: nounwind
define dso_local i64 @F12(ptr noundef %0, i64 noundef %1) #0 {
  %3 = alloca ptr, align 8
  %4 = alloca i64, align 8
  %5 = alloca i64, align 8
  store ptr %0, ptr %3, align 8
  store i64 %1, ptr %4, align 8
  %6 = load ptr, ptr %3, align 8
  %7 = getelementptr inbounds %struct.CPU, ptr %6, i32 0, i32 0
  %8 = load i64, ptr %4, align 8
  %9 = lshr i64 %8, 2
  %10 = and i64 %9, 31
  %11 = getelementptr inbounds [32 x i64], ptr %7, i64 0, i64 %10
  %12 = load i64, ptr %11, align 8
  %13 = load i64, ptr %4, align 8
  %14 = add i64 %12, %13
  store i64 %14, ptr %5, align 8
  %15 = load i64, ptr %5, align 8
  %16 = mul i64 %15, 37
  %17 = load i64, ptr %5, align 8
  %18 = lshr i64 %17, 7
  %19 = xor i64 %16, %18
  store i64 %19, ptr %5, align 8
  %20 = load i64, ptr %5, align 8
  %21 = load ptr, ptr %3, align 8
  %22 = getelementptr inbounds %struct.CPU, ptr %21, i32 0, i32 0
  %23 = load i64, ptr %4, align 8
  %24 = lshr i64 %23, 2
  %25 = and i64 %24, 31
  %26 = getelementptr inbounds [32 x i64], ptr %22, i64 0, i64 %25
  store i64 %20, ptr %26, align 8
  %27 = load ptr, ptr %3, align 8
  %28 = load i64, ptr %4, align 8
  %29 = add i64 %28, 4
  %30 = call i64 @F11(ptr noundef %27, i64 noundef %29)
  %31 = load i64, ptr %5, align 8
  %32 = add i64 %30, %31
  ret i64 %32
}

; Function Attrs: nounwind
define dso_local i64 @F13(ptr noundef %0, i64 noundef %1) #0 {
  %3 = alloca ptr, align 8
  %4 = alloca i64, align 8
  %5 = alloca i64, align 8
  store ptr %0, ptr %3, align 8
  store i64 %1, ptr %4, align 8
  %6 = load ptr, ptr %3, align 8
  %7 = getelementptr inbounds %struct.CPU, ptr %6, i32 0, i32 0
  %8 = load i64, ptr %4, align 8
  %9 = lshr i64 %8, 2
  %10 = and i64 %9, 31
  %11 = getelementptr inbounds [32 x i64], ptr %7, i64 0, i64 %10
  %12 = load i64, ptr %11, align 8
  %13 = load i64, ptr %4, align 8
  %14 = add i64 %12, %13
  store i64 %14, ptr %5, align 8
  %15 = load i64, ptr %5, align 8
  %16 = mul i64 %15, 37
  %17 = load i64, ptr %5, align 8
  %18 = lshr i64 %17, 7
  %19 = xor i64 %16, %18
  store i64 %19, ptr %5, align 8
  %20 = load i64, ptr %5, align 8
  %21 = load ptr, ptr %3, align 8
  %22 = getelementptr inbounds %struct.CPU, ptr %21, i32 0, i32 0
  %23 = load i64, ptr %4, align 8
  %24 = lshr i64 %23, 2
  %25 = and i64 %24, 31
  %26 = getelementptr inbounds [32 x i64], ptr %22, i64 0, i64 %25
  store i64 %20, ptr %26, align 8
  %27 = load ptr, ptr %3, align 8
  %28 = load i64, ptr %4, align 8
  %29 = add i64 %28, 4
  %30 = call i64 @F12(ptr noundef %27, i64 noundef %29)
  %31 = load i64, ptr %5, align 8
  %32 = add i64 %30, %31
  ret i64 %32
}

; Function Attrs: nounwind
define dso_local i64 @F14(ptr noundef %0, i64 noundef %1) #0 {
  %3 = alloca ptr, align 8
  %4 = alloca i64, align 8
  %5 = alloca i64, align 8
  store ptr %0, ptr %3, align 8
  store i64 %1, ptr %4, align 8
  %6 = load ptr, ptr %3, align 8
  %7 = getelementptr inbounds %struct.CPU, ptr %6, i32 0, i32 0
  %8 = load i64, ptr %4, align 8
  %9 = lshr i64 %8, 2
  %10 = and i64 %9, 31
  %11 = getelementptr inbounds [32 x i64], ptr %7, i64 0, i64 %10
  %12 = load i64, ptr %11, align 8
  %13 = load i64, ptr %4, align 8
  %14 = add i64 %12, %13
  store i64 %14, ptr %5, align 8
  %15 = load i64, ptr %5, align 8
  %16 = mul i64 %15, 37
  %17 = load i64, ptr %5, align 8
  %18 = lshr i64 %17, 7
  %19 = xor i64 %16, %18
  store i64 %19, ptr %5, align 8
  %20 = load i64, ptr %5, align 8
  %21 = load ptr, ptr %3, align 8
  %22 = getelementptr inbounds %struct.CPU, ptr %21, i32 0, i32 0
  %23 = load i64, ptr %4, align 8
  %24 = lshr i64 %23, 2
  %25 = and i64 %24, 31
  %26 = getelementptr inbounds [32 x i64], ptr %22, i64 0, i64 %25
  store i64 %20, ptr %26, align 8
  %27 = load ptr, ptr %3, align 8
  %28 = load i64, ptr %4, align 8
  %29 = add i64 %28, 4
  %30 = call i64 @F13(ptr noundef %27, i64 noundef %29)
  %31 = load i64, ptr %5, align 8
  %32 = add i64 %30, %31
  ret i64 %32
}

; Function Attrs: nounwind
define dso_local i64 @F15(ptr noundef %0, i64 noundef %1) #0 {
  %3 = alloca ptr, align 8
  %4 = alloca i64, align 8
  %5 = alloca i64, align 8
  store ptr %0, ptr %3, align 8
  store i64 %1, ptr %4, align 8
  %6 = load ptr, ptr %3, align 8
  %7 = getelementptr inbounds %struct.CPU, ptr %6, i32 0, i32 0
  %8 = load i64, ptr %4, align 8
  %9 = lshr i64 %8, 2
  %10 = and i64 %9, 31
  %11 = getelementptr inbounds [32 x i64], ptr %7, i64 0, i64 %10
  %12 = load i64, ptr %11, align 8
  %13 = load i64, ptr %4, align 8
  %14 = add i64 %12, %13
  store i64 %14, ptr %5, align 8
  %15 = load i64, ptr %5, align 8
  %16 = mul i64 %15, 37
  %17 = load i64, ptr %5, align 8
  %18 = lshr i64 %17, 7
  %19 = xor i64 %16, %18
  store i64 %19, ptr %5, align 8
  %20 = load i64, ptr %5, align 8
  %21 = load ptr, ptr %3, align 8
  %22 = getelementptr inbounds %struct.CPU, ptr %21, i32 0, i32 0
  %23 = load i64, ptr %4, align 8
  %24 = lshr i64 %23, 2
  %25 = and i64 %24, 31
  %26 = getelementptr inbounds [32 x i64], ptr %22, i64 0, i64 %25
  store i64 %20, ptr %26, align 8
  %27 = load ptr, ptr %3, align 8
  %28 = load i64, ptr %4, align 8
  %29 = add i64 %28, 4
  %30 = call i64 @F14(ptr noundef %27, i64 noundef %29)
  %31 = load i64, ptr %5, align 8
  %32 = add i64 %30, %31
  ret i64 %32
}

; Function Attrs: nounwind
define dso_local i64 @F16(ptr noundef %0, i64 noundef %1) #0 {
  %3 = alloca ptr, align 8
  %4 = alloca i64, align 8
  %5 = alloca i64, align 8
  store ptr %0, ptr %3, align 8
  store i64 %1, ptr %4, align 8
  %6 = load ptr, ptr %3, align 8
  %7 = getelementptr inbounds %struct.CPU, ptr %6, i32 0, i32 0
  %8 = load i64, ptr %4, align 8
  %9 = lshr i64 %8, 2
  %10 = and i64 %9, 31
  %11 = getelementptr inbounds [32 x i64], ptr %7, i64 0, i64 %10
  %12 = load i64, ptr %11, align 8
  %13 = load i64, ptr %4, align 8
  %14 = add i64 %12, %13
  store i64 %14, ptr %5, align 8
  %15 = load i64, ptr %5, align 8
  %16 = mul i64 %15, 37
  %17 = load i64, ptr %5, align 8
  %18 = lshr i64 %17, 7
  %19 = xor i64 %16, %18
  store i64 %19, ptr %5, align 8
  %20 = load i64, ptr %5, align 8
  %21 = load ptr, ptr %3, align 8
  %22 = getelementptr inbounds %struct.CPU, ptr %21, i32 0, i32 0
  %23 = load i64, ptr %4, align 8
  %24 = lshr i64 %23, 2
  %25 = and i64 %24, 31
  %26 = getelementptr inbounds [32 x i64], ptr %22, i64 0, i64 %25
  store i64 %20, ptr %26, align 8
  %27 = load ptr, ptr %3, align 8
  %28 = load i64, ptr %4, align 8
  %29 = add i64 %28, 4
  %30 = call i64 @F15(ptr noundef %27, i64 noundef %29)
  %31 = load i64, ptr %5, align 8
  %32 = add i64 %30, %31
  ret i64 %32
}

; Function Attrs: nounwind
define dso_local i64 @F17(ptr noundef %0, i64 noundef %1) #0 {
  %3 = alloca ptr, align 8
  %4 = alloca i64, align 8
  %5 = alloca i64, align 8
  store ptr %0, ptr %3, align 8
  store i64 %1, ptr %4, align 8
  %6 = load ptr, ptr %3, align 8
  %7 = getelementptr inbounds %struct.CPU, ptr %6, i32 0, i32 0
  %8 = load i64, ptr %4, align 8
  %9 = lshr i64 %8, 2
  %10 = and i64 %9, 31
  %11 = getelementptr inbounds [32 x i64], ptr %7, i64 0, i64 %10
  %12 = load i64, ptr %11, align 8
  %13 = load i64, ptr %4, align 8
  %14 = add i64 %12, %13
  store i64 %14, ptr %5, align 8
  %15 = load i64, ptr %5, align 8
  %16 = mul i64 %15, 37
  %17 = load i64, ptr %5, align 8
  %18 = lshr i64 %17, 7
  %19 = xor i64 %16, %18
  store i64 %19, ptr %5, align 8
  %20 = load i64, ptr %5, align 8
  %21 = load ptr, ptr %3, align 8
  %22 = getelementptr inbounds %struct.CPU, ptr %21, i32 0, i32 0
  %23 = load i64, ptr %4, align 8
  %24 = lshr i64 %23, 2
  %25 = and i64 %24, 31
  %26 = getelementptr inbounds [32 x i64], ptr %22, i64 0, i64 %25
  store i64 %20, ptr %26, align 8
  %27 = load ptr, ptr %3, align 8
  %28 = load i64, ptr %4, align 8
  %29 = add i64 %28, 4
  %30 = call i64 @F16(ptr noundef %27, i64 noundef %29)
  %31 = load i64, ptr %5, align 8
  %32 = add i64 %30, %31
  ret i64 %32
}

; Function Attrs: nounwind
define dso_local i64 @F18(ptr noundef %0, i64 noundef %1) #0 {
  %3 = alloca ptr, align 8
  %4 = alloca i64, align 8
  %5 = alloca i64, align 8
  store ptr %0, ptr %3, align 8
  store i64 %1, ptr %4, align 8
  %6 = load ptr, ptr %3, align 8
  %7 = getelementptr inbounds %struct.CPU, ptr %6, i32 0, i32 0
  %8 = load i64, ptr %4, align 8
  %9 = lshr i64 %8, 2
  %10 = and i64 %9, 31
  %11 = getelementptr inbounds [32 x i64], ptr %7, i64 0, i64 %10
  %12 = load i64, ptr %11, align 8
  %13 = load i64, ptr %4, align 8
  %14 = add i64 %12, %13
  store i64 %14, ptr %5, align 8
  %15 = load i64, ptr %5, align 8
  %16 = mul i64 %15, 37
  %17 = load i64, ptr %5, align 8
  %18 = lshr i64 %17, 7
  %19 = xor i64 %16, %18
  store i64 %19, ptr %5, align 8
  %20 = load i64, ptr %5, align 8
  %21 = load ptr, ptr %3, align 8
  %22 = getelementptr inbounds %struct.CPU, ptr %21, i32 0, i32 0
  %23 = load i64, ptr %4, align 8
  %24 = lshr i64 %23, 2
  %25 = and i64 %24, 31
  %26 = getelementptr inbounds [32 x i64], ptr %22, i64 0, i64 %25
  store i64 %20, ptr %26, align 8
  %27 = load ptr, ptr %3, align 8
  %28 = load i64, ptr %4, align 8
  %29 = add i64 %28, 4
  %30 = call i64 @F17(ptr noundef %27, i64 noundef %29)
  %31 = load i64, ptr %5, align 8
  %32 = add i64 %30, %31
  ret i64 %32
}

; Function Attrs: nounwind
define dso_local i64 @F19(ptr noundef %0, i64 noundef %1) #0 {
  %3 = alloca ptr, align 8
  %4 = alloca i64, align 8
  %5 = alloca i64, align 8
  store ptr %0, ptr %3, align 8
  store i64 %1, ptr %4, align 8
  %6 = load ptr, ptr %3, align 8
  %7 = getelementptr inbounds %struct.CPU, ptr %6, i32 0, i32 0
  %8 = load i64, ptr %4, align 8
  %9 = lshr i64 %8, 2
  %10 = and i64 %9, 31
  %11 = getelementptr inbounds [32 x i64], ptr %7, i64 0, i64 %10
  %12 = load i64, ptr %11, align 8
  %13 = load i64, ptr %4, align 8
  %14 = add i64 %12, %13
  store i64 %14, ptr %5, align 8
  %15 = load i64, ptr %5, align 8
  %16 = mul i64 %15, 37
  %17 = load i64, ptr %5, align 8
  %18 = lshr i64 %17, 7
  %19 = xor i64 %16, %18
  store i64 %19, ptr %5, align 8
  %20 = load i64, ptr %5, align 8
  %21 = load ptr, ptr %3, align 8
  %22 = getelementptr inbounds %struct.CPU, ptr %21, i32 0, i32 0
  %23 = load i64, ptr %4, align 8
  %24 = lshr i64 %23, 2
  %25 = and i64 %24, 31
  %26 = getelementptr inbounds [32 x i64], ptr %22, i64 0, i64 %25
  store i64 %20, ptr %26, align 8
  %27 = load ptr, ptr %3, align 8
  %28 = load i64, ptr %4, align 8
  %29 = add i64 %28, 4
  %30 = call i64 @F18(ptr noundef %27, i64 noundef %29)
  %31 = load i64, ptr %5, align 8
  %32 = add i64 %30, %31
  ret i64 %32
}

; Function Attrs: nounwind
define dso_local i64 @F20(ptr noundef %0, i64 noundef %1) #0 {
  %3 = alloca ptr, align 8
  %4 = alloca i64, align 8
  %5 = alloca i64, align 8
  store ptr %0, ptr %3, align 8
  store i64 %1, ptr %4, align 8
  %6 = load ptr, ptr %3, align 8
  %7 = getelementptr inbounds %struct.CPU, ptr %6, i32 0, i32 0
  %8 = load i64, ptr %4, align 8
  %9 = lshr i64 %8, 2
  %10 = and i64 %9, 31
  %11 = getelementptr inbounds [32 x i64], ptr %7, i64 0, i64 %10
  %12 = load i64, ptr %11, align 8
  %13 = load i64, ptr %4, align 8
  %14 = add i64 %12, %13
  store i64 %14, ptr %5, align 8
  %15 = load i64, ptr %5, align 8
  %16 = mul i64 %15, 37
  %17 = load i64, ptr %5, align 8
  %18 = lshr i64 %17, 7
  %19 = xor i64 %16, %18
  store i64 %19, ptr %5, align 8
  %20 = load i64, ptr %5, align 8
  %21 = load ptr, ptr %3, align 8
  %22 = getelementptr inbounds %struct.CPU, ptr %21, i32 0, i32 0
  %23 = load i64, ptr %4, align 8
  %24 = lshr i64 %23, 2
  %25 = and i64 %24, 31
  %26 = getelementptr inbounds [32 x i64], ptr %22, i64 0, i64 %25
  store i64 %20, ptr %26, align 8
  %27 = load ptr, ptr %3, align 8
  %28 = load i64, ptr %4, align 8
  %29 = add i64 %28, 4
  %30 = call i64 @F19(ptr noundef %27, i64 noundef %29)
  %31 = load i64, ptr %5, align 8
  %32 = add i64 %30, %31
  ret i64 %32
}

; Function Attrs: nounwind
define dso_local i64 @F21(ptr noundef %0, i64 noundef %1) #0 {
  %3 = alloca ptr, align 8
  %4 = alloca i64, align 8
  %5 = alloca i64, align 8
  store ptr %0, ptr %3, align 8
  store i64 %1, ptr %4, align 8
  %6 = load ptr, ptr %3, align 8
  %7 = getelementptr inbounds %struct.CPU, ptr %6, i32 0, i32 0
  %8 = load i64, ptr %4, align 8
  %9 = lshr i64 %8, 2
  %10 = and i64 %9, 31
  %11 = getelementptr inbounds [32 x i64], ptr %7, i64 0, i64 %10
  %12 = load i64, ptr %11, align 8
  %13 = load i64, ptr %4, align 8
  %14 = add i64 %12, %13
  store i64 %14, ptr %5, align 8
  %15 = load i64, ptr %5, align 8
  %16 = mul i64 %15, 37
  %17 = load i64, ptr %5, align 8
  %18 = lshr i64 %17, 7
  %19 = xor i64 %16, %18
  store i64 %19, ptr %5, align 8
  %20 = load i64, ptr %5, align 8
  %21 = load ptr, ptr %3, align 8
  %22 = getelementptr inbounds %struct.CPU, ptr %21, i32 0, i32 0
  %23 = load i64, ptr %4, align 8
  %24 = lshr i64 %23, 2
  %25 = and i64 %24, 31
  %26 = getelementptr inbounds [32 x i64], ptr %22, i64 0, i64 %25
  store i64 %20, ptr %26, align 8
  %27 = load ptr, ptr %3, align 8
  %28 = load i64, ptr %4, align 8
  %29 = add i64 %28, 4
  %30 = call i64 @F20(ptr noundef %27, i64 noundef %29)
  %31 = load i64, ptr %5, align 8
  %32 = add i64 %30, %31
  ret i64 %32
}

; Function Attrs: nounwind
define dso_local i64 @F22(ptr noundef %0, i64 noundef %1) #0 {
  %3 = alloca ptr, align 8
  %4 = alloca i64, align 8
  %5 = alloca i64, align 8
  store ptr %0, ptr %3, align 8
  store i64 %1, ptr %4, align 8
  %6 = load ptr, ptr %3, align 8
  %7 = getelementptr inbounds %struct.CPU, ptr %6, i32 0, i32 0
  %8 = load i64, ptr %4, align 8
  %9 = lshr i64 %8, 2
  %10 = and i64 %9, 31
  %11 = getelementptr inbounds [32 x i64], ptr %7, i64 0, i64 %10
  %12 = load i64, ptr %11, align 8
  %13 = load i64, ptr %4, align 8
  %14 = add i64 %12, %13
  store i64 %14, ptr %5, align 8
  %15 = load i64, ptr %5, align 8
  %16 = mul i64 %15, 37
  %17 = load i64, ptr %5, align 8
  %18 = lshr i64 %17, 7
  %19 = xor i64 %16, %18
  store i64 %19, ptr %5, align 8
  %20 = load i64, ptr %5, align 8
  %21 = load ptr, ptr %3, align 8
  %22 = getelementptr inbounds %struct.CPU, ptr %21, i32 0, i32 0
  %23 = load i64, ptr %4, align 8
  %24 = lshr i64 %23, 2
  %25 = and i64 %24, 31
  %26 = getelementptr inbounds [32 x i64], ptr %22, i64 0, i64 %25
  store i64 %20, ptr %26, align 8
  %27 = load ptr, ptr %3, align 8
  %28 = load i64, ptr %4, align 8
  %29 = add i64 %28, 4
  %30 = call i64 @F21(ptr noundef %27, i64 noundef %29)
  %31 = load i64, ptr %5, align 8
  %32 = add i64 %30, %31
  ret i64 %32
}

; Function Attrs: nounwind
define dso_local i64 @F23(ptr noundef %0, i64 noundef %1) #0 {
  %3 = alloca ptr, align 8
  %4 = alloca i64, align 8
  %5 = alloca i64, align 8
  store ptr %0, ptr %3, align 8
  store i64 %1, ptr %4, align 8
  %6 = load ptr, ptr %3, align 8
  %7 = getelementptr inbounds %struct.CPU, ptr %6, i32 0, i32 0
  %8 = load i64, ptr %4, align 8
  %9 = lshr i64 %8, 2
  %10 = and i64 %9, 31
  %11 = getelementptr inbounds [32 x i64], ptr %7, i64 0, i64 %10
  %12 = load i64, ptr %11, align 8
  %13 = load i64, ptr %4, align 8
  %14 = add i64 %12, %13
  store i64 %14, ptr %5, align 8
  %15 = load i64, ptr %5, align 8
  %16 = mul i64 %15, 37
  %17 = load i64, ptr %5, align 8
  %18 = lshr i64 %17, 7
  %19 = xor i64 %16, %18
  store i64 %19, ptr %5, align 8
  %20 = load i64, ptr %5, align 8
  %21 = load ptr, ptr %3, align 8
  %22 = getelementptr inbounds %struct.CPU, ptr %21, i32 0, i32 0
  %23 = load i64, ptr %4, align 8
  %24 = lshr i64 %23, 2
  %25 = and i64 %24, 31
  %26 = getelementptr inbounds [32 x i64], ptr %22, i64 0, i64 %25
  store i64 %20, ptr %26, align 8
  %27 = load ptr, ptr %3, align 8
  %28 = load i64, ptr %4, align 8
  %29 = add i64 %28, 4
  %30 = call i64 @F22(ptr noundef %27, i64 noundef %29)
  %31 = load i64, ptr %5, align 8
  %32 = add i64 %30, %31
  ret i64 %32
}

; Function Attrs: nounwind
define dso_local i64 @F24(ptr noundef %0, i64 noundef %1) #0 {
  %3 = alloca ptr, align 8
  %4 = alloca i64, align 8
  %5 = alloca i64, align 8
  store ptr %0, ptr %3, align 8
  store i64 %1, ptr %4, align 8
  %6 = load ptr, ptr %3, align 8
  %7 = getelementptr inbounds %struct.CPU, ptr %6, i32 0, i32 0
  %8 = load i64, ptr %4, align 8
  %9 = lshr i64 %8, 2
  %10 = and i64 %9, 31
  %11 = getelementptr inbounds [32 x i64], ptr %7, i64 0, i64 %10
  %12 = load i64, ptr %11, align 8
  %13 = load i64, ptr %4, align 8
  %14 = add i64 %12, %13
  store i64 %14, ptr %5, align 8
  %15 = load i64, ptr %5, align 8
  %16 = mul i64 %15, 37
  %17 = load i64, ptr %5, align 8
  %18 = lshr i64 %17, 7
  %19 = xor i64 %16, %18
  store i64 %19, ptr %5, align 8
  %20 = load i64, ptr %5, align 8
  %21 = load ptr, ptr %3, align 8
  %22 = getelementptr inbounds %struct.CPU, ptr %21, i32 0, i32 0
  %23 = load i64, ptr %4, align 8
  %24 = lshr i64 %23, 2
  %25 = and i64 %24, 31
  %26 = getelementptr inbounds [32 x i64], ptr %22, i64 0, i64 %25
  store i64 %20, ptr %26, align 8
  %27 = load ptr, ptr %3, align 8
  %28 = load i64, ptr %4, align 8
  %29 = add i64 %28, 4
  %30 = call i64 @F23(ptr noundef %27, i64 noundef %29)
  %31 = load i64, ptr %5, align 8
  %32 = add i64 %30, %31
  ret i64 %32
}

; Function Attrs: nounwind
define dso_local i64 @F25(ptr noundef %0, i64 noundef %1) #0 {
  %3 = alloca ptr, align 8
  %4 = alloca i64, align 8
  %5 = alloca i64, align 8
  store ptr %0, ptr %3, align 8
  store i64 %1, ptr %4, align 8
  %6 = load ptr, ptr %3, align 8
  %7 = getelementptr inbounds %struct.CPU, ptr %6, i32 0, i32 0
  %8 = load i64, ptr %4, align 8
  %9 = lshr i64 %8, 2
  %10 = and i64 %9, 31
  %11 = getelementptr inbounds [32 x i64], ptr %7, i64 0, i64 %10
  %12 = load i64, ptr %11, align 8
  %13 = load i64, ptr %4, align 8
  %14 = add i64 %12, %13
  store i64 %14, ptr %5, align 8
  %15 = load i64, ptr %5, align 8
  %16 = mul i64 %15, 37
  %17 = load i64, ptr %5, align 8
  %18 = lshr i64 %17, 7
  %19 = xor i64 %16, %18
  store i64 %19, ptr %5, align 8
  %20 = load i64, ptr %5, align 8
  %21 = load ptr, ptr %3, align 8
  %22 = getelementptr inbounds %struct.CPU, ptr %21, i32 0, i32 0
  %23 = load i64, ptr %4, align 8
  %24 = lshr i64 %23, 2
  %25 = and i64 %24, 31
  %26 = getelementptr inbounds [32 x i64], ptr %22, i64 0, i64 %25
  store i64 %20, ptr %26, align 8
  %27 = load ptr, ptr %3, align 8
  %28 = load i64, ptr %4, align 8
  %29 = add i64 %28, 4
  %30 = call i64 @F24(ptr noundef %27, i64 noundef %29)
  %31 = load i64, ptr %5, align 8
  %32 = add i64 %30, %31
  ret i64 %32
}

; Function Attrs: nounwind
define dso_local i64 @F26(ptr noundef %0, i64 noundef %1) #0 {
  %3 = alloca ptr, align 8
  %4 = alloca i64, align 8
  %5 = alloca i64, align 8
  store ptr %0, ptr %3, align 8
  store i64 %1, ptr %4, align 8
  %6 = load ptr, ptr %3, align 8
  %7 = getelementptr inbounds %struct.CPU, ptr %6, i32 0, i32 0
  %8 = load i64, ptr %4, align 8
  %9 = lshr i64 %8, 2
  %10 = and i64 %9, 31
  %11 = getelementptr inbounds [32 x i64], ptr %7, i64 0, i64 %10
  %12 = load i64, ptr %11, align 8
  %13 = load i64, ptr %4, align 8
  %14 = add i64 %12, %13
  store i64 %14, ptr %5, align 8
  %15 = load i64, ptr %5, align 8
  %16 = mul i64 %15, 37
  %17 = load i64, ptr %5, align 8
  %18 = lshr i64 %17, 7
  %19 = xor i64 %16, %18
  store i64 %19, ptr %5, align 8
  %20 = load i64, ptr %5, align 8
  %21 = load ptr, ptr %3, align 8
  %22 = getelementptr inbounds %struct.CPU, ptr %21, i32 0, i32 0
  %23 = load i64, ptr %4, align 8
  %24 = lshr i64 %23, 2
  %25 = and i64 %24, 31
  %26 = getelementptr inbounds [32 x i64], ptr %22, i64 0, i64 %25
  store i64 %20, ptr %26, align 8
  %27 = load ptr, ptr %3, align 8
  %28 = load i64, ptr %4, align 8
  %29 = add i64 %28, 4
  %30 = call i64 @F25(ptr noundef %27, i64 noundef %29)
  %31 = load i64, ptr %5, align 8
  %32 = add i64 %30, %31
  ret i64 %32
}

; Function Attrs: nounwind
define dso_local i64 @F27(ptr noundef %0, i64 noundef %1) #0 {
  %3 = alloca ptr, align 8
  %4 = alloca i64, align 8
  %5 = alloca i64, align 8
  store ptr %0, ptr %3, align 8
  store i64 %1, ptr %4, align 8
  %6 = load ptr, ptr %3, align 8
  %7 = getelementptr inbounds %struct.CPU, ptr %6, i32 0, i32 0
  %8 = load i64, ptr %4, align 8
  %9 = lshr i64 %8, 2
  %10 = and i64 %9, 31
  %11 = getelementptr inbounds [32 x i64], ptr %7, i64 0, i64 %10
  %12 = load i64, ptr %11, align 8
  %13 = load i64, ptr %4, align 8
  %14 = add i64 %12, %13
  store i64 %14, ptr %5, align 8
  %15 = load i64, ptr %5, align 8
  %16 = mul i64 %15, 37
  %17 = load i64, ptr %5, align 8
  %18 = lshr i64 %17, 7
  %19 = xor i64 %16, %18
  store i64 %19, ptr %5, align 8
  %20 = load i64, ptr %5, align 8
  %21 = load ptr, ptr %3, align 8
  %22 = getelementptr inbounds %struct.CPU, ptr %21, i32 0, i32 0
  %23 = load i64, ptr %4, align 8
  %24 = lshr i64 %23, 2
  %25 = and i64 %24, 31
  %26 = getelementptr inbounds [32 x i64], ptr %22, i64 0, i64 %25
  store i64 %20, ptr %26, align 8
  %27 = load ptr, ptr %3, align 8
  %28 = load i64, ptr %4, align 8
  %29 = add i64 %28, 4
  %30 = call i64 @F26(ptr noundef %27, i64 noundef %29)
  %31 = load i64, ptr %5, align 8
  %32 = add i64 %30, %31
  ret i64 %32
}

; Function Attrs: nounwind
define dso_local i64 @F28(ptr noundef %0, i64 noundef %1) #0 {
  %3 = alloca ptr, align 8
  %4 = alloca i64, align 8
  %5 = alloca i64, align 8
  store ptr %0, ptr %3, align 8
  store i64 %1, ptr %4, align 8
  %6 = load ptr, ptr %3, align 8
  %7 = getelementptr inbounds %struct.CPU, ptr %6, i32 0, i32 0
  %8 = load i64, ptr %4, align 8
  %9 = lshr i64 %8, 2
  %10 = and i64 %9, 31
  %11 = getelementptr inbounds [32 x i64], ptr %7, i64 0, i64 %10
  %12 = load i64, ptr %11, align 8
  %13 = load i64, ptr %4, align 8
  %14 = add i64 %12, %13
  store i64 %14, ptr %5, align 8
  %15 = load i64, ptr %5, align 8
  %16 = mul i64 %15, 37
  %17 = load i64, ptr %5, align 8
  %18 = lshr i64 %17, 7
  %19 = xor i64 %16, %18
  store i64 %19, ptr %5, align 8
  %20 = load i64, ptr %5, align 8
  %21 = load ptr, ptr %3, align 8
  %22 = getelementptr inbounds %struct.CPU, ptr %21, i32 0, i32 0
  %23 = load i64, ptr %4, align 8
  %24 = lshr i64 %23, 2
  %25 = and i64 %24, 31
  %26 = getelementptr inbounds [32 x i64], ptr %22, i64 0, i64 %25
  store i64 %20, ptr %26, align 8
  %27 = load ptr, ptr %3, align 8
  %28 = load i64, ptr %4, align 8
  %29 = add i64 %28, 4
  %30 = call i64 @F27(ptr noundef %27, i64 noundef %29)
  %31 = load i64, ptr %5, align 8
  %32 = add i64 %30, %31
  ret i64 %32
}

; Function Attrs: nounwind
define dso_local i64 @F29(ptr noundef %0, i64 noundef %1) #0 {
  %3 = alloca ptr, align 8
  %4 = alloca i64, align 8
  %5 = alloca i64, align 8
  store ptr %0, ptr %3, align 8
  store i64 %1, ptr %4, align 8
  %6 = load ptr, ptr %3, align 8
  %7 = getelementptr inbounds %struct.CPU, ptr %6, i32 0, i32 0
  %8 = load i64, ptr %4, align 8
  %9 = lshr i64 %8, 2
  %10 = and i64 %9, 31
  %11 = getelementptr inbounds [32 x i64], ptr %7, i64 0, i64 %10
  %12 = load i64, ptr %11, align 8
  %13 = load i64, ptr %4, align 8
  %14 = add i64 %12, %13
  store i64 %14, ptr %5, align 8
  %15 = load i64, ptr %5, align 8
  %16 = mul i64 %15, 37
  %17 = load i64, ptr %5, align 8
  %18 = lshr i64 %17, 7
  %19 = xor i64 %16, %18
  store i64 %19, ptr %5, align 8
  %20 = load i64, ptr %5, align 8
  %21 = load ptr, ptr %3, align 8
  %22 = getelementptr inbounds %struct.CPU, ptr %21, i32 0, i32 0
  %23 = load i64, ptr %4, align 8
  %24 = lshr i64 %23, 2
  %25 = and i64 %24, 31
  %26 = getelementptr inbounds [32 x i64], ptr %22, i64 0, i64 %25
  store i64 %20, ptr %26, align 8
  %27 = load ptr, ptr %3, align 8
  %28 = load i64, ptr %4, align 8
  %29 = add i64 %28, 4
  %30 = call i64 @F28(ptr noundef %27, i64 noundef %29)
  %31 = load i64, ptr %5, align 8
  %32 = add i64 %30, %31
  ret i64 %32
}

; Function Attrs: nounwind
define dso_local i64 @F30(ptr noundef %0, i64 noundef %1) #0 {
  %3 = alloca ptr, align 8
  %4 = alloca i64, align 8
  %5 = alloca i64, align 8
  store ptr %0, ptr %3, align 8
  store i64 %1, ptr %4, align 8
  %6 = load ptr, ptr %3, align 8
  %7 = getelementptr inbounds %struct.CPU, ptr %6, i32 0, i32 0
  %8 = load i64, ptr %4, align 8
  %9 = lshr i64 %8, 2
  %10 = and i64 %9, 31
  %11 = getelementptr inbounds [32 x i64], ptr %7, i64 0, i64 %10
  %12 = load i64, ptr %11, align 8
  %13 = load i64, ptr %4, align 8
  %14 = add i64 %12, %13
  store i64 %14, ptr %5, align 8
  %15 = load i64, ptr %5, align 8
  %16 = mul i64 %15, 37
  %17 = load i64, ptr %5, align 8
  %18 = lshr i64 %17, 7
  %19 = xor i64 %16, %18
  store i64 %19, ptr %5, align 8
  %20 = load i64, ptr %5, align 8
  %21 = load ptr, ptr %3, align 8
  %22 = getelementptr inbounds %struct.CPU, ptr %21, i32 0, i32 0
  %23 = load i64, ptr %4, align 8
  %24 = lshr i64 %23, 2
  %25 = and i64 %24, 31
  %26 = getelementptr inbounds [32 x i64], ptr %22, i64 0, i64 %25
  store i64 %20, ptr %26, align 8
  %27 = load ptr, ptr %3, align 8
  %28 = load i64, ptr %4, align 8
  %29 = add i64 %28, 4
  %30 = call i64 @F29(ptr noundef %27, i64 noundef %29)
  %31 = load i64, ptr %5, align 8
  %32 = add i64 %30, %31
  ret i64 %32
}

; Function Attrs: nounwind
define dso_local i64 @F31(ptr noundef %0, i64 noundef %1) #0 {
  %3 = alloca ptr, align 8
  %4 = alloca i64, align 8
  %5 = alloca i64, align 8
  store ptr %0, ptr %3, align 8
  store i64 %1, ptr %4, align 8
  %6 = load ptr, ptr %3, align 8
  %7 = getelementptr inbounds %struct.CPU, ptr %6, i32 0, i32 0
  %8 = load i64, ptr %4, align 8
  %9 = lshr i64 %8, 2
  %10 = and i64 %9, 31
  %11 = getelementptr inbounds [32 x i64], ptr %7, i64 0, i64 %10
  %12 = load i64, ptr %11, align 8
  %13 = load i64, ptr %4, align 8
  %14 = add i64 %12, %13
  store i64 %14, ptr %5, align 8
  %15 = load i64, ptr %5, align 8
  %16 = mul i64 %15, 37
  %17 = load i64, ptr %5, align 8
  %18 = lshr i64 %17, 7
  %19 = xor i64 %16, %18
  store i64 %19, ptr %5, align 8
  %20 = load i64, ptr %5, align 8
  %21 = load ptr, ptr %3, align 8
  %22 = getelementptr inbounds %struct.CPU, ptr %21, i32 0, i32 0
  %23 = load i64, ptr %4, align 8
  %24 = lshr i64 %23, 2
  %25 = and i64 %24, 31
  %26 = getelementptr inbounds [32 x i64], ptr %22, i64 0, i64 %25
  store i64 %20, ptr %26, align 8
  %27 = load ptr, ptr %3, align 8
  %28 = load i64, ptr %4, align 8
  %29 = add i64 %28, 4
  %30 = call i64 @F30(ptr noundef %27, i64 noundef %29)
  %31 = load i64, ptr %5, align 8
  %32 = add i64 %30, %31
  ret i64 %32
}

; Function Attrs: nounwind
define dso_local i64 @F32(ptr noundef %0, i64 noundef %1) #0 {
  %3 = alloca ptr, align 8
  %4 = alloca i64, align 8
  %5 = alloca i64, align 8
  store ptr %0, ptr %3, align 8
  store i64 %1, ptr %4, align 8
  %6 = load ptr, ptr %3, align 8
  %7 = getelementptr inbounds %struct.CPU, ptr %6, i32 0, i32 0
  %8 = load i64, ptr %4, align 8
  %9 = lshr i64 %8, 2
  %10 = and i64 %9, 31
  %11 = getelementptr inbounds [32 x i64], ptr %7, i64 0, i64 %10
  %12 = load i64, ptr %11, align 8
  %13 = load i64, ptr %4, align 8
  %14 = add i64 %12, %13
  store i64 %14, ptr %5, align 8
  %15 = load i64, ptr %5, align 8
  %16 = mul i64 %15, 37
  %17 = load i64, ptr %5, align 8
  %18 = lshr i64 %17, 7
  %19 = xor i64 %16, %18
  store i64 %19, ptr %5, align 8
  %20 = load i64, ptr %5, align 8
  %21 = load ptr, ptr %3, align 8
  %22 = getelementptr inbounds %struct.CPU, ptr %21, i32 0, i32 0
  %23 = load i64, ptr %4, align 8
  %24 = lshr i64 %23, 2
  %25 = and i64 %24, 31
  %26 = getelementptr inbounds [32 x i64], ptr %22, i64 0, i64 %25
  store i64 %20, ptr %26, align 8
  %27 = load ptr, ptr %3, align 8
  %28 = load i64, ptr %4, align 8
  %29 = add i64 %28, 4
  %30 = call i64 @F31(ptr noundef %27, i64 noundef %29)
  %31 = load i64, ptr %5, align 8
  %32 = add i64 %30, %31
  ret i64 %32
}

; Function Attrs: nounwind
define dso_local i64 @F33(ptr noundef %0, i64 noundef %1) #0 {
  %3 = alloca ptr, align 8
  %4 = alloca i64, align 8
  %5 = alloca i64, align 8
  store ptr %0, ptr %3, align 8
  store i64 %1, ptr %4, align 8
  %6 = load ptr, ptr %3, align 8
  %7 = getelementptr inbounds %struct.CPU, ptr %6, i32 0, i32 0
  %8 = load i64, ptr %4, align 8
  %9 = lshr i64 %8, 2
  %10 = and i64 %9, 31
  %11 = getelementptr inbounds [32 x i64], ptr %7, i64 0, i64 %10
  %12 = load i64, ptr %11, align 8
  %13 = load i64, ptr %4, align 8
  %14 = add i64 %12, %13
  store i64 %14, ptr %5, align 8
  %15 = load i64, ptr %5, align 8
  %16 = mul i64 %15, 37
  %17 = load i64, ptr %5, align 8
  %18 = lshr i64 %17, 7
  %19 = xor i64 %16, %18
  store i64 %19, ptr %5, align 8
  %20 = load i64, ptr %5, align 8
  %21 = load ptr, ptr %3, align 8
  %22 = getelementptr inbounds %struct.CPU, ptr %21, i32 0, i32 0
  %23 = load i64, ptr %4, align 8
  %24 = lshr i64 %23, 2
  %25 = and i64 %24, 31
  %26 = getelementptr inbounds [32 x i64], ptr %22, i64 0, i64 %25
  store i64 %20, ptr %26, align 8
  %27 = load ptr, ptr %3, align 8
  %28 = load i64, ptr %4, align 8
  %29 = add i64 %28, 4
  %30 = call i64 @F32(ptr noundef %27, i64 noundef %29)
  %31 = load i64, ptr %5, align 8
  %32 = add i64 %30, %31
  ret i64 %32
}

; Function Attrs: nounwind
define dso_local i64 @F34(ptr noundef %0, i64 noundef %1) #0 {
  %3 = alloca ptr, align 8
  %4 = alloca i64, align 8
  %5 = alloca i64, align 8
  store ptr %0, ptr %3, align 8
  store i64 %1, ptr %4, align 8
  %6 = load ptr, ptr %3, align 8
  %7 = getelementptr inbounds %struct.CPU, ptr %6, i32 0, i32 0
  %8 = load i64, ptr %4, align 8
  %9 = lshr i64 %8, 2
  %10 = and i64 %9, 31
  %11 = getelementptr inbounds [32 x i64], ptr %7, i64 0, i64 %10
  %12 = load i64, ptr %11, align 8
  %13 = load i64, ptr %4, align 8
  %14 = add i64 %12, %13
  store i64 %14, ptr %5, align 8
  %15 = load i64, ptr %5, align 8
  %16 = mul i64 %15, 37
  %17 = load i64, ptr %5, align 8
  %18 = lshr i64 %17, 7
  %19 = xor i64 %16, %18
  store i64 %19, ptr %5, align 8
  %20 = load i64, ptr %5, align 8
  %21 = load ptr, ptr %3, align 8
  %22 = getelementptr inbounds %struct.CPU, ptr %21, i32 0, i32 0
  %23 = load i64, ptr %4, align 8
  %24 = lshr i64 %23, 2
  %25 = and i64 %24, 31
  %26 = getelementptr inbounds [32 x i64], ptr %22, i64 0, i64 %25
  store i64 %20, ptr %26, align 8
  %27 = load ptr, ptr %3, align 8
  %28 = load i64, ptr %4, align 8
  %29 = add i64 %28, 4
  %30 = call i64 @F33(ptr noundef %27, i64 noundef %29)
  %31 = load i64, ptr %5, align 8
  %32 = add i64 %30, %31
  ret i64 %32
}

; Function Attrs: nounwind
define dso_local i64 @F35(ptr noundef %0, i64 noundef %1) #0 {
  %3 = alloca ptr, align 8
  %4 = alloca i64, align 8
  %5 = alloca i64, align 8
  store ptr %0, ptr %3, align 8
  store i64 %1, ptr %4, align 8
  %6 = load ptr, ptr %3, align 8
  %7 = getelementptr inbounds %struct.CPU, ptr %6, i32 0, i32 0
  %8 = load i64, ptr %4, align 8
  %9 = lshr i64 %8, 2
  %10 = and i64 %9, 31
  %11 = getelementptr inbounds [32 x i64], ptr %7, i64 0, i64 %10
  %12 = load i64, ptr %11, align 8
  %13 = load i64, ptr %4, align 8
  %14 = add i64 %12, %13
  store i64 %14, ptr %5, align 8
  %15 = load i64, ptr %5, align 8
  %16 = mul i64 %15, 37
  %17 = load i64, ptr %5, align 8
  %18 = lshr i64 %17, 7
  %19 = xor i64 %16, %18
  store i64 %19, ptr %5, align 8
  %20 = load i64, ptr %5, align 8
  %21 = load ptr, ptr %3, align 8
  %22 = getelementptr inbounds %struct.CPU, ptr %21, i32 0, i32 0
  %23 = load i64, ptr %4, align 8
  %24 = lshr i64 %23, 2
  %25 = and i64 %24, 31
  %26 = getelementptr inbounds [32 x i64], ptr %22, i64 0, i64 %25
  store i64 %20, ptr %26, align 8
  %27 = load ptr, ptr %3, align 8
  %28 = load i64, ptr %4, align 8
  %29 = add i64 %28, 4
  %30 = call i64 @F34(ptr noundef %27, i64 noundef %29)
  %31 = load i64, ptr %5, align 8
  %32 = add i64 %30, %31
  ret i64 %32
}

; Function Attrs: nounwind
define dso_local i64 @F36(ptr noundef %0, i64 noundef %1) #0 {
  %3 = alloca ptr, align 8
  %4 = alloca i64, align 8
  %5 = alloca i64, align 8
  store ptr %0, ptr %3, align 8
  store i64 %1, ptr %4, align 8
  %6 = load ptr, ptr %3, align 8
  %7 = getelementptr inbounds %struct.CPU, ptr %6, i32 0, i32 0
  %8 = load i64, ptr %4, align 8
  %9 = lshr i64 %8, 2
  %10 = and i64 %9, 31
  %11 = getelementptr inbounds [32 x i64], ptr %7, i64 0, i64 %10
  %12 = load i64, ptr %11, align 8
  %13 = load i64, ptr %4, align 8
  %14 = add i64 %12, %13
  store i64 %14, ptr %5, align 8
  %15 = load i64, ptr %5, align 8
  %16 = mul i64 %15, 37
  %17 = load i64, ptr %5, align 8
  %18 = lshr i64 %17, 7
  %19 = xor i64 %16, %18
  store i64 %19, ptr %5, align 8
  %20 = load i64, ptr %5, align 8
  %21 = load ptr, ptr %3, align 8
  %22 = getelementptr inbounds %struct.CPU, ptr %21, i32 0, i32 0
  %23 = load i64, ptr %4, align 8
  %24 = lshr i64 %23, 2
  %25 = and i64 %24, 31
  %26 = getelementptr inbounds [32 x i64], ptr %22, i64 0, i64 %25
  store i64 %20, ptr %26, align 8
  %27 = load ptr, ptr %3, align 8
  %28 = load i64, ptr %4, align 8
  %29 = add i64 %28, 4
  %30 = call i64 @F35(ptr noundef %27, i64 noundef %29)
  %31 = load i64, ptr %5, align 8
  %32 = add i64 %30, %31
  ret i64 %32
}

; Function Attrs: nounwind
define dso_local i64 @F37(ptr noundef %0, i64 noundef %1) #0 {
  %3 = alloca ptr, align 8
  %4 = alloca i64, align 8
  %5 = alloca i64, align 8
  store ptr %0, ptr %3, align 8
  store i64 %1, ptr %4, align 8
  %6 = load ptr, ptr %3, align 8
  %7 = getelementptr inbounds %struct.CPU, ptr %6, i32 0, i32 0
  %8 = load i64, ptr %4, align 8
  %9 = lshr i64 %8, 2
  %10 = and i64 %9, 31
  %11 = getelementptr inbounds [32 x i64], ptr %7, i64 0, i64 %10
  %12 = load i64, ptr %11, align 8
  %13 = load i64, ptr %4, align 8
  %14 = add i64 %12, %13
  store i64 %14, ptr %5, align 8
  %15 = load i64, ptr %5, align 8
  %16 = mul i64 %15, 37
  %17 = load i64, ptr %5, align 8
  %18 = lshr i64 %17, 7
  %19 = xor i64 %16, %18
  store i64 %19, ptr %5, align 8
  %20 = load i64, ptr %5, align 8
  %21 = load ptr, ptr %3, align 8
  %22 = getelementptr inbounds %struct.CPU, ptr %21, i32 0, i32 0
  %23 = load i64, ptr %4, align 8
  %24 = lshr i64 %23, 2
  %25 = and i64 %24, 31
  %26 = getelementptr inbounds [32 x i64], ptr %22, i64 0, i64 %25
  store i64 %20, ptr %26, align 8
  %27 = load ptr, ptr %3, align 8
  %28 = load i64, ptr %4, align 8
  %29 = add i64 %28, 4
  %30 = call i64 @F36(ptr noundef %27, i64 noundef %29)
  %31 = load i64, ptr %5, align 8
  %32 = add i64 %30, %31
  ret i64 %32
}

; Function Attrs: nounwind
define dso_local i64 @F38(ptr noundef %0, i64 noundef %1) #0 {
  %3 = alloca ptr, align 8
  %4 = alloca i64, align 8
  %5 = alloca i64, align 8
  store ptr %0, ptr %3, align 8
  store i64 %1, ptr %4, align 8
  %6 = load ptr, ptr %3, align 8
  %7 = getelementptr inbounds %struct.CPU, ptr %6, i32 0, i32 0
  %8 = load i64, ptr %4, align 8
  %9 = lshr i64 %8, 2
  %10 = and i64 %9, 31
  %11 = getelementptr inbounds [32 x i64], ptr %7, i64 0, i64 %10
  %12 = load i64, ptr %11, align 8
  %13 = load i64, ptr %4, align 8
  %14 = add i64 %12, %13
  store i64 %14, ptr %5, align 8
  %15 = load i64, ptr %5, align 8
  %16 = mul i64 %15, 37
  %17 = load i64, ptr %5, align 8
  %18 = lshr i64 %17, 7
  %19 = xor i64 %16, %18
  store i64 %19, ptr %5, align 8
  %20 = load i64, ptr %5, align 8
  %21 = load ptr, ptr %3, align 8
  %22 = getelementptr inbounds %struct.CPU, ptr %21, i32 0, i32 0
  %23 = load i64, ptr %4, align 8
  %24 = lshr i64 %23, 2
  %25 = and i64 %24, 31
  %26 = getelementptr inbounds [32 x i64], ptr %22, i64 0, i64 %25
  store i64 %20, ptr %26, align 8
  %27 = load ptr, ptr %3, align 8
  %28 = load i64, ptr %4, align 8
  %29 = add i64 %28, 4
  %30 = call i64 @F37(ptr noundef %27, i64 noundef %29)
  %31 = load i64, ptr %5, align 8
  %32 = add i64 %30, %31
  ret i64 %32
}

; Function Attrs: nounwind
define dso_local i64 @F39(ptr noundef %0, i64 noundef %1) #0 {
  %3 = alloca ptr, align 8
  %4 = alloca i64, align 8
  %5 = alloca i64, align 8
  store ptr %0, ptr %3, align 8
  store i64 %1, ptr %4, align 8
  %6 = load ptr, ptr %3, align 8
  %7 = getelementptr inbounds %struct.CPU, ptr %6, i32 0, i32 0
  %8 = load i64, ptr %4, align 8
  %9 = lshr i64 %8, 2
  %10 = and i64 %9, 31
  %11 = getelementptr inbounds [32 x i64], ptr %7, i64 0, i64 %10
  %12 = load i64, ptr %11, align 8
  %13 = load i64, ptr %4, align 8
  %14 = add i64 %12, %13
  store i64 %14, ptr %5, align 8
  %15 = load i64, ptr %5, align 8
  %16 = mul i64 %15, 37
  %17 = load i64, ptr %5, align 8
  %18 = lshr i64 %17, 7
  %19 = xor i64 %16, %18
  store i64 %19, ptr %5, align 8
  %20 = load i64, ptr %5, align 8
  %21 = load ptr, ptr %3, align 8
  %22 = getelementptr inbounds %struct.CPU, ptr %21, i32 0, i32 0
  %23 = load i64, ptr %4, align 8
  %24 = lshr i64 %23, 2
  %25 = and i64 %24, 31
  %26 = getelementptr inbounds [32 x i64], ptr %22, i64 0, i64 %25
  store i64 %20, ptr %26, align 8
  %27 = load ptr, ptr %3, align 8
  %28 = load i64, ptr %4, align 8
  %29 = add i64 %28, 4
  %30 = call i64 @F38(ptr noundef %27, i64 noundef %29)
  %31 = load i64, ptr %5, align 8
  %32 = add i64 %30, %31
  ret i64 %32
}

; Function Attrs: nounwind
define dso_local i64 @F40(ptr noundef %0, i64 noundef %1) #0 {
  %3 = alloca ptr, align 8
  %4 = alloca i64, align 8
  %5 = alloca i64, align 8
  store ptr %0, ptr %3, align 8
  store i64 %1, ptr %4, align 8
  %6 = load ptr, ptr %3, align 8
  %7 = getelementptr inbounds %struct.CPU, ptr %6, i32 0, i32 0
  %8 = load i64, ptr %4, align 8
  %9 = lshr i64 %8, 2
  %10 = and i64 %9, 31
  %11 = getelementptr inbounds [32 x i64], ptr %7, i64 0, i64 %10
  %12 = load i64, ptr %11, align 8
  %13 = load i64, ptr %4, align 8
  %14 = add i64 %12, %13
  store i64 %14, ptr %5, align 8
  %15 = load i64, ptr %5, align 8
  %16 = mul i64 %15, 37
  %17 = load i64, ptr %5, align 8
  %18 = lshr i64 %17, 7
  %19 = xor i64 %16, %18
  store i64 %19, ptr %5, align 8
  %20 = load i64, ptr %5, align 8
  %21 = load ptr, ptr %3, align 8
  %22 = getelementptr inbounds %struct.CPU, ptr %21, i32 0, i32 0
  %23 = load i64, ptr %4, align 8
  %24 = lshr i64 %23, 2
  %25 = and i64 %24, 31
  %26 = getelementptr inbounds [32 x i64], ptr %22, i64 0, i64 %25
  store i64 %20, ptr %26, align 8
  %27 = load ptr, ptr %3, align 8
  %28 = load i64, ptr %4, align 8
  %29 = add i64 %28, 4
  %30 = call i64 @F39(ptr noundef %27, i64 noundef %29)
  %31 = load i64, ptr %5, align 8
  %32 = add i64 %30, %31
  ret i64 %32
}

; Function Attrs: nounwind
define dso_local i64 @F41(ptr noundef %0, i64 noundef %1) #0 {
  %3 = alloca ptr, align 8
  %4 = alloca i64, align 8
  %5 = alloca i64, align 8
  store ptr %0, ptr %3, align 8
  store i64 %1, ptr %4, align 8
  %6 = load ptr, ptr %3, align 8
  %7 = getelementptr inbounds %struct.CPU, ptr %6, i32 0, i32 0
  %8 = load i64, ptr %4, align 8
  %9 = lshr i64 %8, 2
  %10 = and i64 %9, 31
  %11 = getelementptr inbounds [32 x i64], ptr %7, i64 0, i64 %10
  %12 = load i64, ptr %11, align 8
  %13 = load i64, ptr %4, align 8
  %14 = add i64 %12, %13
  store i64 %14, ptr %5, align 8
  %15 = load i64, ptr %5, align 8
  %16 = mul i64 %15, 37
  %17 = load i64, ptr %5, align 8
  %18 = lshr i64 %17, 7
  %19 = xor i64 %16, %18
  store i64 %19, ptr %5, align 8
  %20 = load i64, ptr %5, align 8
  %21 = load ptr, ptr %3, align 8
  %22 = getelementptr inbounds %struct.CPU, ptr %21, i32 0, i32 0
  %23 = load i64, ptr %4, align 8
  %24 = lshr i64 %23, 2
  %25 = and i64 %24, 31
  %26 = getelementptr inbounds [32 x i64], ptr %22, i64 0, i64 %25
  store i64 %20, ptr %26, align 8
  %27 = load ptr, ptr %3, align 8
  %28 = load i64, ptr %4, align 8
  %29 = add i64 %28, 4
  %30 = call i64 @F40(ptr noundef %27, i64 noundef %29)
  %31 = load i64, ptr %5, align 8
  %32 = add i64 %30, %31
  ret i64 %32
}

; Function Attrs: nounwind
define dso_local i64 @F42(ptr noundef %0, i64 noundef %1) #0 {
  %3 = alloca ptr, align 8
  %4 = alloca i64, align 8
  %5 = alloca i64, align 8
  store ptr %0, ptr %3, align 8
  store i64 %1, ptr %4, align 8
  %6 = load ptr, ptr %3, align 8
  %7 = getelementptr inbounds %struct.CPU, ptr %6, i32 0, i32 0
  %8 = load i64, ptr %4, align 8
  %9 = lshr i64 %8, 2
  %10 = and i64 %9, 31
  %11 = getelementptr inbounds [32 x i64], ptr %7, i64 0, i64 %10
  %12 = load i64, ptr %11, align 8
  %13 = load i64, ptr %4, align 8
  %14 = add i64 %12, %13
  store i64 %14, ptr %5, align 8
  %15 = load i64, ptr %5, align 8
  %16 = mul i64 %15, 37
  %17 = load i64, ptr %5, align 8
  %18 = lshr i64 %17, 7
  %19 = xor i64 %16, %18
  store i64 %19, ptr %5, align 8
  %20 = load i64, ptr %5, align 8
  %21 = load ptr, ptr %3, align 8
  %22 = getelementptr inbounds %struct.CPU, ptr %21, i32 0, i32 0
  %23 = load i64, ptr %4, align 8
  %24 = lshr i64 %23, 2
  %25 = and i64 %24, 31
  %26 = getelementptr inbounds [32 x i64], ptr %22, i64 0, i64 %25
  store i64 %20, ptr %26, align 8
  %27 = load ptr, ptr %3, align 8
  %28 = load i64, ptr %4, align 8
  %29 = add i64 %28, 4
  %30 = call i64 @F41(ptr noundef %27, i64 noundef %29)
  %31 = load i64, ptr %5, align 8
  %32 = add i64 %30, %31
  ret i64 %32
}

; Function Attrs: nounwind
define dso_local i64 @F43(ptr noundef %0, i64 noundef %1) #0 {
  %3 = alloca ptr, align 8
  %4 = alloca i64, align 8
  %5 = alloca i64, align 8
  store ptr %0, ptr %3, align 8
  store i64 %1, ptr %4, align 8
  %6 = load ptr, ptr %3, align 8
  %7 = getelementptr inbounds %struct.CPU, ptr %6, i32 0, i32 0
  %8 = load i64, ptr %4, align 8
  %9 = lshr i64 %8, 2
  %10 = and i64 %9, 31
  %11 = getelementptr inbounds [32 x i64], ptr %7, i64 0, i64 %10
  %12 = load i64, ptr %11, align 8
  %13 = load i64, ptr %4, align 8
  %14 = add i64 %12, %13
  store i64 %14, ptr %5, align 8
  %15 = load i64, ptr %5, align 8
  %16 = mul i64 %15, 37
  %17 = load i64, ptr %5, align 8
  %18 = lshr i64 %17, 7
  %19 = xor i64 %16, %18
  store i64 %19, ptr %5, align 8
  %20 = load i64, ptr %5, align 8
  %21 = load ptr, ptr %3, align 8
  %22 = getelementptr inbounds %struct.CPU, ptr %21, i32 0, i32 0
  %23 = load i64, ptr %4, align 8
  %24 = lshr i64 %23, 2
  %25 = and i64 %24, 31
  %26 = getelementptr inbounds [32 x i64], ptr %22, i64 0, i64 %25
  store i64 %20, ptr %26, align 8
  %27 = load ptr, ptr %3, align 8
  %28 = load i64, ptr %4, align 8
  %29 = add i64 %28, 4
  %30 = call i64 @F42(ptr noundef %27, i64 noundef %29)
  %31 = load i64, ptr %5, align 8
  %32 = add i64 %30, %31
  ret i64 %32
}

; Function Attrs: nounwind
define dso_local i64 @F44(ptr noundef %0, i64 noundef %1) #0 {
  %3 = alloca ptr, align 8
  %4 = alloca i64, align 8
  %5 = alloca i64, align 8
  store ptr %0, ptr %3, align 8
  store i64 %1, ptr %4, align 8
  %6 = load ptr, ptr %3, align 8
  %7 = getelementptr inbounds %struct.CPU, ptr %6, i32 0, i32 0
  %8 = load i64, ptr %4, align 8
  %9 = lshr i64 %8, 2
  %10 = and i64 %9, 31
  %11 = getelementptr inbounds [32 x i64], ptr %7, i64 0, i64 %10
  %12 = load i64, ptr %11, align 8
  %13 = load i64, ptr %4, align 8
  %14 = add i64 %12, %13
  store i64 %14, ptr %5, align 8
  %15 = load i64, ptr %5, align 8
  %16 = mul i64 %15, 37
  %17 = load i64, ptr %5, align 8
  %18 = lshr i64 %17, 7
  %19 = xor i64 %16, %18
  store i64 %19, ptr %5, align 8
  %20 = load i64, ptr %5, align 8
  %21 = load ptr, ptr %3, align 8
  %22 = getelementptr inbounds %struct.CPU, ptr %21, i32 0, i32 0
  %23 = load i64, ptr %4, align 8
  %24 = lshr i64 %23, 2
  %25 = and i64 %24, 31
  %26 = getelementptr inbounds [32 x i64], ptr %22, i64 0, i64 %25
  store i64 %20, ptr %26, align 8
  %27 = load ptr, ptr %3, align 8
  %28 = load i64, ptr %4, align 8
  %29 = add i64 %28, 4
  %30 = call i64 @F43(ptr noundef %27, i64 noundef %29)
  %31 = load i64, ptr %5, align 8
  %32 = add i64 %30, %31
  ret i64 %32
}

; Function Attrs: nounwind
define dso_local i64 @F45(ptr noundef %0, i64 noundef %1) #0 {
  %3 = alloca ptr, align 8
  %4 = alloca i64, align 8
  %5 = alloca i64, align 8
  store ptr %0, ptr %3, align 8
  store i64 %1, ptr %4, align 8
  %6 = load ptr, ptr %3, align 8
  %7 = getelementptr inbounds %struct.CPU, ptr %6, i32 0, i32 0
  %8 = load i64, ptr %4, align 8
  %9 = lshr i64 %8, 2
  %10 = and i64 %9, 31
  %11 = getelementptr inbounds [32 x i64], ptr %7, i64 0, i64 %10
  %12 = load i64, ptr %11, align 8
  %13 = load i64, ptr %4, align 8
  %14 = add i64 %12, %13
  store i64 %14, ptr %5, align 8
  %15 = load i64, ptr %5, align 8
  %16 = mul i64 %15, 37
  %17 = load i64, ptr %5, align 8
  %18 = lshr i64 %17, 7
  %19 = xor i64 %16, %18
  store i64 %19, ptr %5, align 8
  %20 = load i64, ptr %5, align 8
  %21 = load ptr, ptr %3, align 8
  %22 = getelementptr inbounds %struct.CPU, ptr %21, i32 0, i32 0
  %23 = load i64, ptr %4, align 8
  %24 = lshr i64 %23, 2
  %25 = and i64 %24, 31
  %26 = getelementptr inbounds [32 x i64], ptr %22, i64 0, i64 %25
  store i64 %20, ptr %26, align 8
  %27 = load ptr, ptr %3, align 8
  %28 = load i64, ptr %4, align 8
  %29 = add i64 %28, 4
  %30 = call i64 @F44(ptr noundef %27, i64 noundef %29)
  %31 = load i64, ptr %5, align 8
  %32 = add i64 %30, %31
  ret i64 %32
}

; Function Attrs: nounwind
define dso_local i64 @F46(ptr noundef %0, i64 noundef %1) #0 {
  %3 = alloca ptr, align 8
  %4 = alloca i64, align 8
  %5 = alloca i64, align 8
  store ptr %0, ptr %3, align 8
  store i64 %1, ptr %4, align 8
  %6 = load ptr, ptr %3, align 8
  %7 = getelementptr inbounds %struct.CPU, ptr %6, i32 0, i32 0
  %8 = load i64, ptr %4, align 8
  %9 = lshr i64 %8, 2
  %10 = and i64 %9, 31
  %11 = getelementptr inbounds [32 x i64], ptr %7, i64 0, i64 %10
  %12 = load i64, ptr %11, align 8
  %13 = load i64, ptr %4, align 8
  %14 = add i64 %12, %13
  store i64 %14, ptr %5, align 8
  %15 = load i64, ptr %5, align 8
  %16 = mul i64 %15, 37
  %17 = load i64, ptr %5, align 8
  %18 = lshr i64 %17, 7
  %19 = xor i64 %16, %18
  store i64 %19, ptr %5, align 8
  %20 = load i64, ptr %5, align 8
  %21 = load ptr, ptr %3, align 8
  %22 = getelementptr inbounds %struct.CPU, ptr %21, i32 0, i32 0
  %23 = load i64, ptr %4, align 8
  %24 = lshr i64 %23, 2
  %25 = and i64 %24, 31
  %26 = getelementptr inbounds [32 x i64], ptr %22, i64 0, i64 %25
  store i64 %20, ptr %26, align 8
  %27 = load ptr, ptr %3, align 8
  %28 = load i64, ptr %4, align 8
  %29 = add i64 %28, 4
  %30 = call i64 @F45(ptr noundef %27, i64 noundef %29)
  %31 = load i64, ptr %5, align 8
  %32 = add i64 %30, %31
  ret i64 %32
}

; Function Attrs: nounwind
define dso_local i64 @F47(ptr noundef %0, i64 noundef %1) #0 {
  %3 = alloca ptr, align 8
  %4 = alloca i64, align 8
  %5 = alloca i64, align 8
  store ptr %0, ptr %3, align 8
  store i64 %1, ptr %4, align 8
  %6 = load ptr, ptr %3, align 8
  %7 = getelementptr inbounds %struct.CPU, ptr %6, i32 0, i32 0
  %8 = load i64, ptr %4, align 8
  %9 = lshr i64 %8, 2
  %10 = and i64 %9, 31
  %11 = getelementptr inbounds [32 x i64], ptr %7, i64 0, i64 %10
  %12 = load i64, ptr %11, align 8
  %13 = load i64, ptr %4, align 8
  %14 = add i64 %12, %13
  store i64 %14, ptr %5, align 8
  %15 = load i64, ptr %5, align 8
  %16 = mul i64 %15, 37
  %17 = load i64, ptr %5, align 8
  %18 = lshr i64 %17, 7
  %19 = xor i64 %16, %18
  store i64 %19, ptr %5, align 8
  %20 = load i64, ptr %5, align 8
  %21 = load ptr, ptr %3, align 8
  %22 = getelementptr inbounds %struct.CPU, ptr %21, i32 0, i32 0
  %23 = load i64, ptr %4, align 8
  %24 = lshr i64 %23, 2
  %25 = and i64 %24, 31
  %26 = getelementptr inbounds [32 x i64], ptr %22, i64 0, i64 %25
  store i64 %20, ptr %26, align 8
  %27 = load ptr, ptr %3, align 8
  %28 = load i64, ptr %4, align 8
  %29 = add i64 %28, 4
  %30 = call i64 @F46(ptr noundef %27, i64 noundef %29)
  %31 = load i64, ptr %5, align 8
  %32 = add i64 %30, %31
  ret i64 %32
}

; Function Attrs: nounwind
define dso_local i64 @F48(ptr noundef %0, i64 noundef %1) #0 {
  %3 = alloca ptr, align 8
  %4 = alloca i64, align 8
  %5 = alloca i64, align 8
  store ptr %0, ptr %3, align 8
  store i64 %1, ptr %4, align 8
  %6 = load ptr, ptr %3, align 8
  %7 = getelementptr inbounds %struct.CPU, ptr %6, i32 0, i32 0
  %8 = load i64, ptr %4, align 8
  %9 = lshr i64 %8, 2
  %10 = and i64 %9, 31
  %11 = getelementptr inbounds [32 x i64], ptr %7, i64 0, i64 %10
  %12 = load i64, ptr %11, align 8
  %13 = load i64, ptr %4, align 8
  %14 = add i64 %12, %13
  store i64 %14, ptr %5, align 8
  %15 = load i64, ptr %5, align 8
  %16 = mul i64 %15, 37
  %17 = load i64, ptr %5, align 8
  %18 = lshr i64 %17, 7
  %19 = xor i64 %16, %18
  store i64 %19, ptr %5, align 8
  %20 = load i64, ptr %5, align 8
  %21 = load ptr, ptr %3, align 8
  %22 = getelementptr inbounds %struct.CPU, ptr %21, i32 0, i32 0
  %23 = load i64, ptr %4, align 8
  %24 = lshr i64 %23, 2
  %25 = and i64 %24, 31
  %26 = getelementptr inbounds [32 x i64], ptr %22, i64 0, i64 %25
  store i64 %20, ptr %26, align 8
  %27 = load ptr, ptr %3, align 8
  %28 = load i64, ptr %4, align 8
  %29 = add i64 %28, 4
  %30 = call i64 @F47(ptr noundef %27, i64 noundef %29)
  %31 = load i64, ptr %5, align 8
  %32 = add i64 %30, %31
  ret i64 %32
}

; Function Attrs: nounwind
define dso_local i64 @F49(ptr noundef %0, i64 noundef %1) #0 {
  %3 = alloca ptr, align 8
  %4 = alloca i64, align 8
  %5 = alloca i64, align 8
  store ptr %0, ptr %3, align 8
  store i64 %1, ptr %4, align 8
  %6 = load ptr, ptr %3, align 8
  %7 = getelementptr inbounds %struct.CPU, ptr %6, i32 0, i32 0
  %8 = load i64, ptr %4, align 8
  %9 = lshr i64 %8, 2
  %10 = and i64 %9, 31
  %11 = getelementptr inbounds [32 x i64], ptr %7, i64 0, i64 %10
  %12 = load i64, ptr %11, align 8
  %13 = load i64, ptr %4, align 8
  %14 = add i64 %12, %13
  store i64 %14, ptr %5, align 8
  %15 = load i64, ptr %5, align 8
  %16 = mul i64 %15, 37
  %17 = load i64, ptr %5, align 8
  %18 = lshr i64 %17, 7
  %19 = xor i64 %16, %18
  store i64 %19, ptr %5, align 8
  %20 = load i64, ptr %5, align 8
  %21 = load ptr, ptr %3, align 8
  %22 = getelementptr inbounds %struct.CPU, ptr %21, i32 0, i32 0
  %23 = load i64, ptr %4, align 8
  %24 = lshr i64 %23, 2
  %25 = and i64 %24, 31
  %26 = getelementptr inbounds [32 x i64], ptr %22, i64 0, i64 %25
  store i64 %20, ptr %26, align 8
  %27 = load ptr, ptr %3, align 8
  %28 = load i64, ptr %4, align 8
  %29 = add i64 %28, 4
  %30 = call i64 @F48(ptr noundef %27, i64 noundef %29)
  %31 = load i64, ptr %5, align 8
  %32 = add i64 %30, %31
  ret i64 %32
}

; Function Attrs: nounwind
define dso_local i64 @F50(ptr noundef %0, i64 noundef %1) #0 {
  %3 = alloca ptr, align 8
  %4 = alloca i64, align 8
  %5 = alloca i64, align 8
  store ptr %0, ptr %3, align 8
  store i64 %1, ptr %4, align 8
  %6 = load ptr, ptr %3, align 8
  %7 = getelementptr inbounds %struct.CPU, ptr %6, i32 0, i32 0
  %8 = load i64, ptr %4, align 8
  %9 = lshr i64 %8, 2
  %10 = and i64 %9, 31
  %11 = getelementptr inbounds [32 x i64], ptr %7, i64 0, i64 %10
  %12 = load i64, ptr %11, align 8
  %13 = load i64, ptr %4, align 8
  %14 = add i64 %12, %13
  store i64 %14, ptr %5, align 8
  %15 = load i64, ptr %5, align 8
  %16 = mul i64 %15, 37
  %17 = load i64, ptr %5, align 8
  %18 = lshr i64 %17, 7
  %19 = xor i64 %16, %18
  store i64 %19, ptr %5, align 8
  %20 = load i64, ptr %5, align 8
  %21 = load ptr, ptr %3, align 8
  %22 = getelementptr inbounds %struct.CPU, ptr %21, i32 0, i32 0
  %23 = load i64, ptr %4, align 8
  %24 = lshr i64 %23, 2
  %25 = and i64 %24, 31
  %26 = getelementptr inbounds [32 x i64], ptr %22, i64 0, i64 %25
  store i64 %20, ptr %26, align 8
  %27 = load ptr, ptr %3, align 8
  %28 = load i64, ptr %4, align 8
  %29 = add i64 %28, 4
  %30 = call i64 @F49(ptr noundef %27, i64 noundef %29)
  %31 = load i64, ptr %5, align 8
  %32 = add i64 %30, %31
  ret i64 %32
}

; Function Attrs: nounwind
define dso_local i64 @F51(ptr noundef %0, i64 noundef %1) #0 {
  %3 = alloca ptr, align 8
  %4 = alloca i64, align 8
  %5 = alloca i64, align 8
  store ptr %0, ptr %3, align 8
  store i64 %1, ptr %4, align 8
  %6 = load ptr, ptr %3, align 8
  %7 = getelementptr inbounds %struct.CPU, ptr %6, i32 0, i32 0
  %8 = load i64, ptr %4, align 8
  %9 = lshr i64 %8, 2
  %10 = and i64 %9, 31
  %11 = getelementptr inbounds [32 x i64], ptr %7, i64 0, i64 %10
  %12 = load i64, ptr %11, align 8
  %13 = load i64, ptr %4, align 8
  %14 = add i64 %12, %13
  store i64 %14, ptr %5, align 8
  %15 = load i64, ptr %5, align 8
  %16 = mul i64 %15, 37
  %17 = load i64, ptr %5, align 8
  %18 = lshr i64 %17, 7
  %19 = xor i64 %16, %18
  store i64 %19, ptr %5, align 8
  %20 = load i64, ptr %5, align 8
  %21 = load ptr, ptr %3, align 8
  %22 = getelementptr inbounds %struct.CPU, ptr %21, i32 0, i32 0
  %23 = load i64, ptr %4, align 8
  %24 = lshr i64 %23, 2
  %25 = and i64 %24, 31
  %26 = getelementptr inbounds [32 x i64], ptr %22, i64 0, i64 %25
  store i64 %20, ptr %26, align 8
  %27 = load ptr, ptr %3, align 8
  %28 = load i64, ptr %4, align 8
  %29 = add i64 %28, 4
  %30 = call i64 @F50(ptr noundef %27, i64 noundef %29)
  %31 = load i64, ptr %5, align 8
  %32 = add i64 %30, %31
  ret i64 %32
}

; Function Attrs: nounwind
define dso_local i64 @F52(ptr noundef %0, i64 noundef %1) #0 {
  %3 = alloca ptr, align 8
  %4 = alloca i64, align 8
  %5 = alloca i64, align 8
  store ptr %0, ptr %3, align 8
  store i64 %1, ptr %4, align 8
  %6 = load ptr, ptr %3, align 8
  %7 = getelementptr inbounds %struct.CPU, ptr %6, i32 0, i32 0
  %8 = load i64, ptr %4, align 8
  %9 = lshr i64 %8, 2
  %10 = and i64 %9, 31
  %11 = getelementptr inbounds [32 x i64], ptr %7, i64 0, i64 %10
  %12 = load i64, ptr %11, align 8
  %13 = load i64, ptr %4, align 8
  %14 = add i64 %12, %13
  store i64 %14, ptr %5, align 8
  %15 = load i64, ptr %5, align 8
  %16 = mul i64 %15, 37
  %17 = load i64, ptr %5, align 8
  %18 = lshr i64 %17, 7
  %19 = xor i64 %16, %18
  store i64 %19, ptr %5, align 8
  %20 = load i64, ptr %5, align 8
  %21 = load ptr, ptr %3, align 8
  %22 = getelementptr inbounds %struct.CPU, ptr %21, i32 0, i32 0
  %23 = load i64, ptr %4, align 8
  %24 = lshr i64 %23, 2
  %25 = and i64 %24, 31
  %26 = getelementptr inbounds [32 x i64], ptr %22, i64 0, i64 %25
  store i64 %20, ptr %26, align 8
  %27 = load ptr, ptr %3, align 8
  %28 = load i64, ptr %4, align 8
  %29 = add i64 %28, 4
  %30 = call i64 @F51(ptr noundef %27, i64 noundef %29)
  %31 = load i64, ptr %5, align 8
  %32 = add i64 %30, %31
  ret i64 %32
}

; Function Attrs: nounwind
define dso_local i64 @F53(ptr noundef %0, i64 noundef %1) #0 {
  %3 = alloca ptr, align 8
  %4 = alloca i64, align 8
  %5 = alloca i64, align 8
  store ptr %0, ptr %3, align 8
  store i64 %1, ptr %4, align 8
  %6 = load ptr, ptr %3, align 8
  %7 = getelementptr inbounds %struct.CPU, ptr %6, i32 0, i32 0
  %8 = load i64, ptr %4, align 8
  %9 = lshr i64 %8, 2
  %10 = and i64 %9, 31
  %11 = getelementptr inbounds [32 x i64], ptr %7, i64 0, i64 %10
  %12 = load i64, ptr %11, align 8
  %13 = load i64, ptr %4, align 8
  %14 = add i64 %12, %13
  store i64 %14, ptr %5, align 8
  %15 = load i64, ptr %5, align 8
  %16 = mul i64 %15, 37
  %17 = load i64, ptr %5, align 8
  %18 = lshr i64 %17, 7
  %19 = xor i64 %16, %18
  store i64 %19, ptr %5, align 8
  %20 = load i64, ptr %5, align 8
  %21 = load ptr, ptr %3, align 8
  %22 = getelementptr inbounds %struct.CPU, ptr %21, i32 0, i32 0
  %23 = load i64, ptr %4, align 8
  %24 = lshr i64 %23, 2
  %25 = and i64 %24, 31
  %26 = getelementptr inbounds [32 x i64], ptr %22, i64 0, i64 %25
  store i64 %20, ptr %26, align 8
  %27 = load ptr, ptr %3, align 8
  %28 = load i64, ptr %4, align 8
  %29 = add i64 %28, 4
  %30 = call i64 @F52(ptr noundef %27, i64 noundef %29)
  %31 = load i64, ptr %5, align 8
  %32 = add i64 %30, %31
  ret i64 %32
}

; Function Attrs: nounwind
define dso_local i64 @F54(ptr noundef %0, i64 noundef %1) #0 {
  %3 = alloca ptr, align 8
  %4 = alloca i64, align 8
  %5 = alloca i64, align 8
  store ptr %0, ptr %3, align 8
  store i64 %1, ptr %4, align 8
  %6 = load ptr, ptr %3, align 8
  %7 = getelementptr inbounds %struct.CPU, ptr %6, i32 0, i32 0
  %8 = load i64, ptr %4, align 8
  %9 = lshr i64 %8, 2
  %10 = and i64 %9, 31
  %11 = getelementptr inbounds [32 x i64], ptr %7, i64 0, i64 %10
  %12 = load i64, ptr %11, align 8
  %13 = load i64, ptr %4, align 8
  %14 = add i64 %12, %13
  store i64 %14, ptr %5, align 8
  %15 = load i64, ptr %5, align 8
  %16 = mul i64 %15, 37
  %17 = load i64, ptr %5, align 8
  %18 = lshr i64 %17, 7
  %19 = xor i64 %16, %18
  store i64 %19, ptr %5, align 8
  %20 = load i64, ptr %5, align 8
  %21 = load ptr, ptr %3, align 8
  %22 = getelementptr inbounds %struct.CPU, ptr %21, i32 0, i32 0
  %23 = load i64, ptr %4, align 8
  %24 = lshr i64 %23, 2
  %25 = and i64 %24, 31
  %26 = getelementptr inbounds [32 x i64], ptr %22, i64 0, i64 %25
  store i64 %20, ptr %26, align 8
  %27 = load ptr, ptr %3, align 8
  %28 = load i64, ptr %4, align 8
  %29 = add i64 %28, 4
  %30 = call i64 @F53(ptr noundef %27, i64 noundef %29)
  %31 = load i64, ptr %5, align 8
  %32 = add i64 %30, %31
  ret i64 %32
}

; Function Attrs: nounwind
define dso_local i64 @F55(ptr noundef %0, i64 noundef %1) #0 {
  %3 = alloca ptr, align 8
  %4 = alloca i64, align 8
  %5 = alloca i64, align 8
  store ptr %0, ptr %3, align 8
  store i64 %1, ptr %4, align 8
  %6 = load ptr, ptr %3, align 8
  %7 = getelementptr inbounds %struct.CPU, ptr %6, i32 0, i32 0
  %8 = load i64, ptr %4, align 8
  %9 = lshr i64 %8, 2
  %10 = and i64 %9, 31
  %11 = getelementptr inbounds [32 x i64], ptr %7, i64 0, i64 %10
  %12 = load i64, ptr %11, align 8
  %13 = load i64, ptr %4, align 8
  %14 = add i64 %12, %13
  store i64 %14, ptr %5, align 8
  %15 = load i64, ptr %5, align 8
  %16 = mul i64 %15, 37
  %17 = load i64, ptr %5, align 8
  %18 = lshr i64 %17, 7
  %19 = xor i64 %16, %18
  store i64 %19, ptr %5, align 8
  %20 = load i64, ptr %5, align 8
  %21 = load ptr, ptr %3, align 8
  %22 = getelementptr inbounds %struct.CPU, ptr %21, i32 0, i32 0
  %23 = load i64, ptr %4, align 8
  %24 = lshr i64 %23, 2
  %25 = and i64 %24, 31
  %26 = getelementptr inbounds [32 x i64], ptr %22, i64 0, i64 %25
  store i64 %20, ptr %26, align 8
  %27 = load ptr, ptr %3, align 8
  %28 = load i64, ptr %4, align 8
  %29 = add i64 %28, 4
  %30 = call i64 @F54(ptr noundef %27, i64 noundef %29)
  %31 = load i64, ptr %5, align 8
  %32 = add i64 %30, %31
  ret i64 %32
}

; Function Attrs: nounwind
define dso_local i64 @F56(ptr noundef %0, i64 noundef %1) #0 {
  %3 = alloca ptr, align 8
  %4 = alloca i64, align 8
  %5 = alloca i64, align 8
  store ptr %0, ptr %3, align 8
  store i64 %1, ptr %4, align 8
  %6 = load ptr, ptr %3, align 8
  %7 = getelementptr inbounds %struct.CPU, ptr %6, i32 0, i32 0
  %8 = load i64, ptr %4, align 8
  %9 = lshr i64 %8, 2
  %10 = and i64 %9, 31
  %11 = getelementptr inbounds [32 x i64], ptr %7, i64 0, i64 %10
  %12 = load i64, ptr %11, align 8
  %13 = load i64, ptr %4, align 8
  %14 = add i64 %12, %13
  store i64 %14, ptr %5, align 8
  %15 = load i64, ptr %5, align 8
  %16 = mul i64 %15, 37
  %17 = load i64, ptr %5, align 8
  %18 = lshr i64 %17, 7
  %19 = xor i64 %16, %18
  store i64 %19, ptr %5, align 8
  %20 = load i64, ptr %5, align 8
  %21 = load ptr, ptr %3, align 8
  %22 = getelementptr inbounds %struct.CPU, ptr %21, i32 0, i32 0
  %23 = load i64, ptr %4, align 8
  %24 = lshr i64 %23, 2
  %25 = and i64 %24, 31
  %26 = getelementptr inbounds [32 x i64], ptr %22, i64 0, i64 %25
  store i64 %20, ptr %26, align 8
  %27 = load ptr, ptr %3, align 8
  %28 = load i64, ptr %4, align 8
  %29 = add i64 %28, 4
  %30 = call i64 @F55(ptr noundef %27, i64 noundef %29)
  %31 = load i64, ptr %5, align 8
  %32 = add i64 %30, %31
  ret i64 %32
}

; Function Attrs: nounwind
define dso_local i64 @F57(ptr noundef %0, i64 noundef %1) #0 {
  %3 = alloca ptr, align 8
  %4 = alloca i64, align 8
  %5 = alloca i64, align 8
  store ptr %0, ptr %3, align 8
  store i64 %1, ptr %4, align 8
  %6 = load ptr, ptr %3, align 8
  %7 = getelementptr inbounds %struct.CPU, ptr %6, i32 0, i32 0
  %8 = load i64, ptr %4, align 8
  %9 = lshr i64 %8, 2
  %10 = and i64 %9, 31
  %11 = getelementptr inbounds [32 x i64], ptr %7, i64 0, i64 %10
  %12 = load i64, ptr %11, align 8
  %13 = load i64, ptr %4, align 8
  %14 = add i64 %12, %13
  store i64 %14, ptr %5, align 8
  %15 = load i64, ptr %5, align 8
  %16 = mul i64 %15, 37
  %17 = load i64, ptr %5, align 8
  %18 = lshr i64 %17, 7
  %19 = xor i64 %16, %18
  store i64 %19, ptr %5, align 8
  %20 = load i64, ptr %5, align 8
  %21 = load ptr, ptr %3, align 8
  %22 = getelementptr inbounds %struct.CPU, ptr %21, i32 0, i32 0
  %23 = load i64, ptr %4, align 8
  %24 = lshr i64 %23, 2
  %25 = and i64 %24, 31
  %26 = getelementptr inbounds [32 x i64], ptr %22, i64 0, i64 %25
  store i64 %20, ptr %26, align 8
  %27 = load ptr, ptr %3, align 8
  %28 = load i64, ptr %4, align 8
  %29 = add i64 %28, 4
  %30 = call i64 @F56(ptr noundef %27, i64 noundef %29)
  %31 = load i64, ptr %5, align 8
  %32 = add i64 %30, %31
  ret i64 %32
}

; Function Attrs: nounwind
define dso_local i64 @F58(ptr noundef %0, i64 noundef %1) #0 {
  %3 = alloca ptr, align 8
  %4 = alloca i64, align 8
  %5 = alloca i64, align 8
  store ptr %0, ptr %3, align 8
  store i64 %1, ptr %4, align 8
  %6 = load ptr, ptr %3, align 8
  %7 = getelementptr inbounds %struct.CPU, ptr %6, i32 0, i32 0
  %8 = load i64, ptr %4, align 8
  %9 = lshr i64 %8, 2
  %10 = and i64 %9, 31
  %11 = getelementptr inbounds [32 x i64], ptr %7, i64 0, i64 %10
  %12 = load i64, ptr %11, align 8
  %13 = load i64, ptr %4, align 8
  %14 = add i64 %12, %13
  store i64 %14, ptr %5, align 8
  %15 = load i64, ptr %5, align 8
  %16 = mul i64 %15, 37
  %17 = load i64, ptr %5, align 8
  %18 = lshr i64 %17, 7
  %19 = xor i64 %16, %18
  store i64 %19, ptr %5, align 8
  %20 = load i64, ptr %5, align 8
  %21 = load ptr, ptr %3, align 8
  %22 = getelementptr inbounds %struct.CPU, ptr %21, i32 0, i32 0
  %23 = load i64, ptr %4, align 8
  %24 = lshr i64 %23, 2
  %25 = and i64 %24, 31
  %26 = getelementptr inbounds [32 x i64], ptr %22, i64 0, i64 %25
  store i64 %20, ptr %26, align 8
  %27 = load ptr, ptr %3, align 8
  %28 = load i64, ptr %4, align 8
  %29 = add i64 %28, 4
  %30 = call i64 @F57(ptr noundef %27, i64 noundef %29)
  %31 = load i64, ptr %5, align 8
  %32 = add i64 %30, %31
  ret i64 %32
}

; Function Attrs: nounwind
define dso_local i64 @F59(ptr noundef %0, i64 noundef %1) #0 {
  %3 = alloca ptr, align 8
  %4 = alloca i64, align 8
  %5 = alloca i64, align 8
  store ptr %0, ptr %3, align 8
  store i64 %1, ptr %4, align 8
  %6 = load ptr, ptr %3, align 8
  %7 = getelementptr inbounds %struct.CPU, ptr %6, i32 0, i32 0
  %8 = load i64, ptr %4, align 8
  %9 = lshr i64 %8, 2
  %10 = and i64 %9, 31
  %11 = getelementptr inbounds [32 x i64], ptr %7, i64 0, i64 %10
  %12 = load i64, ptr %11, align 8
  %13 = load i64, ptr %4, align 8
  %14 = add i64 %12, %13
  store i64 %14, ptr %5, align 8
  %15 = load i64, ptr %5, align 8
  %16 = mul i64 %15, 37
  %17 = load i64, ptr %5, align 8
  %18 = lshr i64 %17, 7
  %19 = xor i64 %16, %18
  store i64 %19, ptr %5, align 8
  %20 = load i64, ptr %5, align 8
  %21 = load ptr, ptr %3, align 8
  %22 = getelementptr inbounds %struct.CPU, ptr %21, i32 0, i32 0
  %23 = load i64, ptr %4, align 8
  %24 = lshr i64 %23, 2
  %25 = and i64 %24, 31
  %26 = getelementptr inbounds [32 x i64], ptr %22, i64 0, i64 %25
  store i64 %20, ptr %26, align 8
  %27 = load ptr, ptr %3, align 8
  %28 = load i64, ptr %4, align 8
  %29 = add i64 %28, 4
  %30 = call i64 @F58(ptr noundef %27, i64 noundef %29)
  %31 = load i64, ptr %5, align 8
  %32 = add i64 %30, %31
  ret i64 %32
}

; Function Attrs: nounwind
define dso_local i64 @F60(ptr noundef %0, i64 noundef %1) #0 {
  %3 = alloca ptr, align 8
  %4 = alloca i64, align 8
  %5 = alloca i64, align 8
  store ptr %0, ptr %3, align 8
  store i64 %1, ptr %4, align 8
  %6 = load ptr, ptr %3, align 8
  %7 = getelementptr inbounds %struct.CPU, ptr %6, i32 0, i32 0
  %8 = load i64, ptr %4, align 8
  %9 = lshr i64 %8, 2
  %10 = and i64 %9, 31
  %11 = getelementptr inbounds [32 x i64], ptr %7, i64 0, i64 %10
  %12 = load i64, ptr %11, align 8
  %13 = load i64, ptr %4, align 8
  %14 = add i64 %12, %13
  store i64 %14, ptr %5, align 8
  %15 = load i64, ptr %5, align 8
  %16 = mul i64 %15, 37
  %17 = load i64, ptr %5, align 8
  %18 = lshr i64 %17, 7
  %19 = xor i64 %16, %18
  store i64 %19, ptr %5, align 8
  %20 = load i64, ptr %5, align 8
  %21 = load ptr, ptr %3, align 8
  %22 = getelementptr inbounds %struct.CPU, ptr %21, i32 0, i32 0
  %23 = load i64, ptr %4, align 8
  %24 = lshr i64 %23, 2
  %25 = and i64 %24, 31
  %26 = getelementptr inbounds [32 x i64], ptr %22, i64 0, i64 %25
  store i64 %20, ptr %26, align 8
  %27 = load ptr, ptr %3, align 8
  %28 = load i64, ptr %4, align 8
  %29 = add i64 %28, 4
  %30 = call i64 @F59(ptr noundef %27, i64 noundef %29)
  %31 = load i64, ptr %5, align 8
  %32 = add i64 %30, %31
  ret i64 %32
}

; Function Attrs: nounwind
define dso_local i64 @F61(ptr noundef %0, i64 noundef %1) #0 {
  %3 = alloca ptr, align 8
  %4 = alloca i64, align 8
  %5 = alloca i64, align 8
  store ptr %0, ptr %3, align 8
  store i64 %1, ptr %4, align 8
  %6 = load ptr, ptr %3, align 8
  %7 = getelementptr inbounds %struct.CPU, ptr %6, i32 0, i32 0
  %8 = load i64, ptr %4, align 8
  %9 = lshr i64 %8, 2
  %10 = and i64 %9, 31
  %11 = getelementptr inbounds [32 x i64], ptr %7, i64 0, i64 %10
  %12 = load i64, ptr %11, align 8
  %13 = load i64, ptr %4, align 8
  %14 = add i64 %12, %13
  store i64 %14, ptr %5, align 8
  %15 = load i64, ptr %5, align 8
  %16 = mul i64 %15, 37
  %17 = load i64, ptr %5, align 8
  %18 = lshr i64 %17, 7
  %19 = xor i64 %16, %18
  store i64 %19, ptr %5, align 8
  %20 = load i64, ptr %5, align 8
  %21 = load ptr, ptr %3, align 8
  %22 = getelementptr inbounds %struct.CPU, ptr %21, i32 0, i32 0
  %23 = load i64, ptr %4, align 8
  %24 = lshr i64 %23, 2
  %25 = and i64 %24, 31
  %26 = getelementptr inbounds [32 x i64], ptr %22, i64 0, i64 %25
  store i64 %20, ptr %26, align 8
  %27 = load ptr, ptr %3, align 8
  %28 = load i64, ptr %4, align 8
  %29 = add i64 %28, 4
  %30 = call i64 @F60(ptr noundef %27, i64 noundef %29)
  %31 = load i64, ptr %5, align 8
  %32 = add i64 %30, %31
  ret i64 %32
}

; Function Attrs: nounwind
define dso_local i64 @F62(ptr noundef %0, i64 noundef %1) #0 {
  %3 = alloca ptr, align 8
  %4 = alloca i64, align 8
  %5 = alloca i64, align 8
  store ptr %0, ptr %3, align 8
  store i64 %1, ptr %4, align 8
  %6 = load ptr, ptr %3, align 8
  %7 = getelementptr inbounds %struct.CPU, ptr %6, i32 0, i32 0
  %8 = load i64, ptr %4, align 8
  %9 = lshr i64 %8, 2
  %10 = and i64 %9, 31
  %11 = getelementptr inbounds [32 x i64], ptr %7, i64 0, i64 %10
  %12 = load i64, ptr %11, align 8
  %13 = load i64, ptr %4, align 8
  %14 = add i64 %12, %13
  store i64 %14, ptr %5, align 8
  %15 = load i64, ptr %5, align 8
  %16 = mul i64 %15, 37
  %17 = load i64, ptr %5, align 8
  %18 = lshr i64 %17, 7
  %19 = xor i64 %16, %18
  store i64 %19, ptr %5, align 8
  %20 = load i64, ptr %5, align 8
  %21 = load ptr, ptr %3, align 8
  %22 = getelementptr inbounds %struct.CPU, ptr %21, i32 0, i32 0
  %23 = load i64, ptr %4, align 8
  %24 = lshr i64 %23, 2
  %25 = and i64 %24, 31
  %26 = getelementptr inbounds [32 x i64], ptr %22, i64 0, i64 %25
  store i64 %20, ptr %26, align 8
  %27 = load ptr, ptr %3, align 8
  %28 = load i64, ptr %4, align 8
  %29 = add i64 %28, 4
  %30 = call i64 @F61(ptr noundef %27, i64 noundef %29)
  %31 = load i64, ptr %5, align 8
  %32 = add i64 %30, %31
  ret i64 %32
}

; Function Attrs: nounwind
define dso_local i64 @F63(ptr noundef %0, i64 noundef %1) #0 {
  %3 = alloca ptr, align 8
  %4 = alloca i64, align 8
  %5 = alloca i64, align 8
  store ptr %0, ptr %3, align 8
  store i64 %1, ptr %4, align 8
  %6 = load ptr, ptr %3, align 8
  %7 = getelementptr inbounds %struct.CPU, ptr %6, i32 0, i32 0
  %8 = load i64, ptr %4, align 8
  %9 = lshr i64 %8, 2
  %10 = and i64 %9, 31
  %11 = getelementptr inbounds [32 x i64], ptr %7, i64 0, i64 %10
  %12 = load i64, ptr %11, align 8
  %13 = load i64, ptr %4, align 8
  %14 = add i64 %12, %13
  store i64 %14, ptr %5, align 8
  %15 = load i64, ptr %5, align 8
  %16 = mul i64 %15, 37
  %17 = load i64, ptr %5, align 8
  %18 = lshr i64 %17, 7
  %19 = xor i64 %16, %18
  store i64 %19, ptr %5, align 8
  %20 = load i64, ptr %5, align 8
  %21 = load ptr, ptr %3, align 8
  %22 = getelementptr inbounds %struct.CPU, ptr %21, i32 0, i32 0
  %23 = load i64, ptr %4, align 8
  %24 = lshr i64 %23, 2
  %25 = and i64 %24, 31
  %26 = getelementptr inbounds [32 x i64], ptr %22, i64 0, i64 %25
  store i64 %20, ptr %26, align 8
  %27 = load ptr, ptr %3, align 8
  %28 = load i64, ptr %4, align 8
  %29 = add i64 %28, 4
  %30 = call i64 @F62(ptr noundef %27, i64 noundef %29)
  %31 = load i64, ptr %5, align 8
  %32 = add i64 %30, %31
  ret i64 %32
}

; Function Attrs: nounwind
define dso_local i64 @F64(ptr noundef %0, i64 noundef %1) #0 {
  %3 = alloca ptr, align 8
  %4 = alloca i64, align 8
  %5 = alloca i64, align 8
  store ptr %0, ptr %3, align 8
  store i64 %1, ptr %4, align 8
  %6 = load ptr, ptr %3, align 8
  %7 = getelementptr inbounds %struct.CPU, ptr %6, i32 0, i32 0
  %8 = load i64, ptr %4, align 8
  %9 = lshr i64 %8, 2
  %10 = and i64 %9, 31
  %11 = getelementptr inbounds [32 x i64], ptr %7, i64 0, i64 %10
  %12 = load i64, ptr %11, align 8
  %13 = load i64, ptr %4, align 8
  %14 = add i64 %12, %13
  store i64 %14, ptr %5, align 8
  %15 = load i64, ptr %5, align 8
  %16 = mul i64 %15, 37
  %17 = load i64, ptr %5, align 8
  %18 = lshr i64 %17, 7
  %19 = xor i64 %16, %18
  store i64 %19, ptr %5, align 8
  %20 = load i64, ptr %5, align 8
  %21 = load ptr, ptr %3, align 8
  %22 = getelementptr inbounds %struct.CPU, ptr %21, i32 0, i32 0
  %23 = load i64, ptr %4, align 8
  %24 = lshr i64 %23, 2
  %25 = and i64 %24, 31
  %26 = getelementptr inbounds [32 x i64], ptr %22, i64 0, i64 %25
  store i64 %20, ptr %26, align 8
  %27 = load ptr, ptr %3, align 8
  %28 = load i64, ptr %4, align 8
  %29 = add i64 %28, 4
  %30 = call i64 @F63(ptr noundef %27, i64 noundef %29)
  %31 = load i64, ptr %5, align 8
  %32 = add i64 %30, %31
  ret i64 %32
}

; Function Attrs: nounwind
define dso_local i64 @F65(ptr noundef %0, i64 noundef %1) #0 {
  %3 = alloca ptr, align 8
  %4 = alloca i64, align 8
  %5 = alloca i64, align 8
  store ptr %0, ptr %3, align 8
  store i64 %1, ptr %4, align 8
  %6 = load ptr, ptr %3, align 8
  %7 = getelementptr inbounds %struct.CPU, ptr %6, i32 0, i32 0
  %8 = load i64, ptr %4, align 8
  %9 = lshr i64 %8, 2
  %10 = and i64 %9, 31
  %11 = getelementptr inbounds [32 x i64], ptr %7, i64 0, i64 %10
  %12 = load i64, ptr %11, align 8
  %13 = load i64, ptr %4, align 8
  %14 = add i64 %12, %13
  store i64 %14, ptr %5, align 8
  %15 = load i64, ptr %5, align 8
  %16 = mul i64 %15, 37
  %17 = load i64, ptr %5, align 8
  %18 = lshr i64 %17, 7
  %19 = xor i64 %16, %18
  store i64 %19, ptr %5, align 8
  %20 = load i64, ptr %5, align 8
  %21 = load ptr, ptr %3, align 8
  %22 = getelementptr inbounds %struct.CPU, ptr %21, i32 0, i32 0
  %23 = load i64, ptr %4, align 8
  %24 = lshr i64 %23, 2
  %25 = and i64 %24, 31
  %26 = getelementptr inbounds [32 x i64], ptr %22, i64 0, i64 %25
  store i64 %20, ptr %26, align 8
  %27 = load ptr, ptr %3, align 8
  %28 = load i64, ptr %4, align 8
  %29 = add i64 %28, 4
  %30 = call i64 @F64(ptr noundef %27, i64 noundef %29)
  %31 = load i64, ptr %5, align 8
  %32 = add i64 %30, %31
  ret i64 %32
}

; Function Attrs: nounwind
define dso_local i64 @F66(ptr noundef %0, i64 noundef %1) #0 {
  %3 = alloca ptr, align 8
  %4 = alloca i64, align 8
  %5 = alloca i64, align 8
  store ptr %0, ptr %3, align 8
  store i64 %1, ptr %4, align 8
  %6 = load ptr, ptr %3, align 8
  %7 = getelementptr inbounds %struct.CPU, ptr %6, i32 0, i32 0
  %8 = load i64, ptr %4, align 8
  %9 = lshr i64 %8, 2
  %10 = and i64 %9, 31
  %11 = getelementptr inbounds [32 x i64], ptr %7, i64 0, i64 %10
  %12 = load i64, ptr %11, align 8
  %13 = load i64, ptr %4, align 8
  %14 = add i64 %12, %13
  store i64 %14, ptr %5, align 8
  %15 = load i64, ptr %5, align 8
  %16 = mul i64 %15, 37
  %17 = load i64, ptr %5, align 8
  %18 = lshr i64 %17, 7
  %19 = xor i64 %16, %18
  store i64 %19, ptr %5, align 8
  %20 = load i64, ptr %5, align 8
  %21 = load ptr, ptr %3, align 8
  %22 = getelementptr inbounds %struct.CPU, ptr %21, i32 0, i32 0
  %23 = load i64, ptr %4, align 8
  %24 = lshr i64 %23, 2
  %25 = and i64 %24, 31
  %26 = getelementptr inbounds [32 x i64], ptr %22, i64 0, i64 %25
  store i64 %20, ptr %26, align 8
  %27 = load ptr, ptr %3, align 8
  %28 = load i64, ptr %4, align 8
  %29 = add i64 %28, 4
  %30 = call i64 @F65(ptr noundef %27, i64 noundef %29)
  %31 = load i64, ptr %5, align 8
  %32 = add i64 %30, %31
  ret i64 %32
}

; Function Attrs: nounwind
define dso_local i64 @F67(ptr noundef %0, i64 noundef %1) #0 {
  %3 = alloca ptr, align 8
  %4 = alloca i64, align 8
  %5 = alloca i64, align 8
  store ptr %0, ptr %3, align 8
  store i64 %1, ptr %4, align 8
  %6 = load ptr, ptr %3, align 8
  %7 = getelementptr inbounds %struct.CPU, ptr %6, i32 0, i32 0
  %8 = load i64, ptr %4, align 8
  %9 = lshr i64 %8, 2
  %10 = and i64 %9, 31
  %11 = getelementptr inbounds [32 x i64], ptr %7, i64 0, i64 %10
  %12 = load i64, ptr %11, align 8
  %13 = load i64, ptr %4, align 8
  %14 = add i64 %12, %13
  store i64 %14, ptr %5, align 8
  %15 = load i64, ptr %5, align 8
  %16 = mul i64 %15, 37
  %17 = load i64, ptr %5, align 8
  %18 = lshr i64 %17, 7
  %19 = xor i64 %16, %18
  store i64 %19, ptr %5, align 8
  %20 = load i64, ptr %5, align 8
  %21 = load ptr, ptr %3, align 8
  %22 = getelementptr inbounds %struct.CPU, ptr %21, i32 0, i32 0
  %23 = load i64, ptr %4, align 8
  %24 = lshr i64 %23, 2
  %25 = and i64 %24, 31
  %26 = getelementptr inbounds [32 x i64], ptr %22, i64 0, i64 %25
  store i64 %20, ptr %26, align 8
  %27 = load ptr, ptr %3, align 8
  %28 = load i64, ptr %4, align 8
  %29 = add i64 %28, 4
  %30 = call i64 @F66(ptr noundef %27, i64 noundef %29)
  %31 = load i64, ptr %5, align 8
  %32 = add i64 %30, %31
  ret i64 %32
}

; Function Attrs: nounwind
define dso_local i64 @F68(ptr noundef %0, i64 noundef %1) #0 {
  %3 = alloca ptr, align 8
  %4 = alloca i64, align 8
  %5 = alloca i64, align 8
  store ptr %0, ptr %3, align 8
  store i64 %1, ptr %4, align 8
  %6 = load ptr, ptr %3, align 8
  %7 = getelementptr inbounds %struct.CPU, ptr %6, i32 0, i32 0
  %8 = load i64, ptr %4, align 8
  %9 = lshr i64 %8, 2
  %10 = and i64 %9, 31
  %11 = getelementptr inbounds [32 x i64], ptr %7, i64 0, i64 %10
  %12 = load i64, ptr %11, align 8
  %13 = load i64, ptr %4, align 8
  %14 = add i64 %12, %13
  store i64 %14, ptr %5, align 8
  %15 = load i64, ptr %5, align 8
  %16 = mul i64 %15, 37
  %17 = load i64, ptr %5, align 8
  %18 = lshr i64 %17, 7
  %19 = xor i64 %16, %18
  store i64 %19, ptr %5, align 8
  %20 = load i64, ptr %5, align 8
  %21 = load ptr, ptr %3, align 8
  %22 = getelementptr inbounds %struct.CPU, ptr %21, i32 0, i32 0
  %23 = load i64, ptr %4, align 8
  %24 = lshr i64 %23, 2
  %25 = and i64 %24, 31
  %26 = getelementptr inbounds [32 x i64], ptr %22, i64 0, i64 %25
  store i64 %20, ptr %26, align 8
  %27 = load ptr, ptr %3, align 8
  %28 = load i64, ptr %4, align 8
  %29 = add i64 %28, 4
  %30 = call i64 @F67(ptr noundef %27, i64 noundef %29)
  %31 = load i64, ptr %5, align 8
  %32 = add i64 %30, %31
  ret i64 %32
}

; Function Attrs: nounwind
define dso_local i64 @F69(ptr noundef %0, i64 noundef %1) #0 {
  %3 = alloca ptr, align 8
  %4 = alloca i64, align 8
  %5 = alloca i64, align 8
  store ptr %0, ptr %3, align 8
  store i64 %1, ptr %4, align 8
  %6 = load ptr, ptr %3, align 8
  %7 = getelementptr inbounds %struct.CPU, ptr %6, i32 0, i32 0
  %8 = load i64, ptr %4, align 8
  %9 = lshr i64 %8, 2
  %10 = and i64 %9, 31
  %11 = getelementptr inbounds [32 x i64], ptr %7, i64 0, i64 %10
  %12 = load i64, ptr %11, align 8
  %13 = load i64, ptr %4, align 8
  %14 = add i64 %12, %13
  store i64 %14, ptr %5, align 8
  %15 = load i64, ptr %5, align 8
  %16 = mul i64 %15, 37
  %17 = load i64, ptr %5, align 8
  %18 = lshr i64 %17, 7
  %19 = xor i64 %16, %18
  store i64 %19, ptr %5, align 8
  %20 = load i64, ptr %5, align 8
  %21 = load ptr, ptr %3, align 8
  %22 = getelementptr inbounds %struct.CPU, ptr %21, i32 0, i32 0
  %23 = load i64, ptr %4, align 8
  %24 = lshr i64 %23, 2
  %25 = and i64 %24, 31
  %26 = getelementptr inbounds [32 x i64], ptr %22, i64 0, i64 %25
  store i64 %20, ptr %26, align 8
  %27 = load ptr, ptr %3, align 8
  %28 = load i64, ptr %4, align 8
  %29 = add i64 %28, 4
  %30 = call i64 @F68(ptr noundef %27, i64 noundef %29)
  %31 = load i64, ptr %5, align 8
  %32 = add i64 %30, %31
  ret i64 %32
}

; Function Attrs: nounwind
define dso_local i64 @F70(ptr noundef %0, i64 noundef %1) #0 {
  %3 = alloca ptr, align 8
  %4 = alloca i64, align 8
  %5 = alloca i64, align 8
  store ptr %0, ptr %3, align 8
  store i64 %1, ptr %4, align 8
  %6 = load ptr, ptr %3, align 8
  %7 = getelementptr inbounds %struct.CPU, ptr %6, i32 0, i32 0
  %8 = load i64, ptr %4, align 8
  %9 = lshr i64 %8, 2
  %10 = and i64 %9, 31
  %11 = getelementptr inbounds [32 x i64], ptr %7, i64 0, i64 %10
  %12 = load i64, ptr %11, align 8
  %13 = load i64, ptr %4, align 8
  %14 = add i64 %12, %13
  store i64 %14, ptr %5, align 8
  %15 = load i64, ptr %5, align 8
  %16 = mul i64 %15, 37
  %17 = load i64, ptr %5, align 8
  %18 = lshr i64 %17, 7
  %19 = xor i64 %16, %18
  store i64 %19, ptr %5, align 8
  %20 = load i64, ptr %5, align 8
  %21 = load ptr, ptr %3, align 8
  %22 = getelementptr inbounds %struct.CPU, ptr %21, i32 0, i32 0
  %23 = load i64, ptr %4, align 8
  %24 = lshr i64 %23, 2
  %25 = and i64 %24, 31
  %26 = getelementptr inbounds [32 x i64], ptr %22, i64 0, i64 %25
  store i64 %20, ptr %26, align 8
  %27 = load ptr, ptr %3, align 8
  %28 = load i64, ptr %4, align 8
  %29 = add i64 %28, 4
  %30 = call i64 @F69(ptr noundef %27, i64 noundef %29)
  %31 = load i64, ptr %5, align 8
  %32 = add i64 %30, %31
  ret i64 %32
}

; Function Attrs: nounwind
define dso_local i64 @F71(ptr noundef %0, i64 noundef %1) #0 {
  %3 = alloca ptr, align 8
  %4 = alloca i64, align 8
  %5 = alloca i64, align 8
  store ptr %0, ptr %3, align 8
  store i64 %1, ptr %4, align 8
  %6 = load ptr, ptr %3, align 8
  %7 = getelementptr inbounds %struct.CPU, ptr %6, i32 0, i32 0
  %8 = load i64, ptr %4, align 8
  %9 = lshr i64 %8, 2
  %10 = and i64 %9, 31
  %11 = getelementptr inbounds [32 x i64], ptr %7, i64 0, i64 %10
  %12 = load i64, ptr %11, align 8
  %13 = load i64, ptr %4, align 8
  %14 = add i64 %12, %13
  store i64 %14, ptr %5, align 8
  %15 = load i64, ptr %5, align 8
  %16 = mul i64 %15, 37
  %17 = load i64, ptr %5, align 8
  %18 = lshr i64 %17, 7
  %19 = xor i64 %16, %18
  store i64 %19, ptr %5, align 8
  %20 = load i64, ptr %5, align 8
  %21 = load ptr, ptr %3, align 8
  %22 = getelementptr inbounds %struct.CPU, ptr %21, i32 0, i32 0
  %23 = load i64, ptr %4, align 8
  %24 = lshr i64 %23, 2
  %25 = and i64 %24, 31
  %26 = getelementptr inbounds [32 x i64], ptr %22, i64 0, i64 %25
  store i64 %20, ptr %26, align 8
  %27 = load ptr, ptr %3, align 8
  %28 = load i64, ptr %4, align 8
  %29 = add i64 %28, 4
  %30 = call i64 @F70(ptr noundef %27, i64 noundef %29)
  %31 = load i64, ptr %5, align 8
  %32 = add i64 %30, %31
  ret i64 %32
}

; Function Attrs: nounwind
define dso_local i64 @F72(ptr noundef %0, i64 noundef %1) #0 {
  %3 = alloca ptr, align 8
  %4 = alloca i64, align 8
  %5 = alloca i64, align 8
  store ptr %0, ptr %3, align 8
  store i64 %1, ptr %4, align 8
  %6 = load ptr, ptr %3, align 8
  %7 = getelementptr inbounds %struct.CPU, ptr %6, i32 0, i32 0
  %8 = load i64, ptr %4, align 8
  %9 = lshr i64 %8, 2
  %10 = and i64 %9, 31
  %11 = getelementptr inbounds [32 x i64], ptr %7, i64 0, i64 %10
  %12 = load i64, ptr %11, align 8
  %13 = load i64, ptr %4, align 8
  %14 = add i64 %12, %13
  store i64 %14, ptr %5, align 8
  %15 = load i64, ptr %5, align 8
  %16 = mul i64 %15, 37
  %17 = load i64, ptr %5, align 8
  %18 = lshr i64 %17, 7
  %19 = xor i64 %16, %18
  store i64 %19, ptr %5, align 8
  %20 = load i64, ptr %5, align 8
  %21 = load ptr, ptr %3, align 8
  %22 = getelementptr inbounds %struct.CPU, ptr %21, i32 0, i32 0
  %23 = load i64, ptr %4, align 8
  %24 = lshr i64 %23, 2
  %25 = and i64 %24, 31
  %26 = getelementptr inbounds [32 x i64], ptr %22, i64 0, i64 %25
  store i64 %20, ptr %26, align 8
  %27 = load ptr, ptr %3, align 8
  %28 = load i64, ptr %4, align 8
  %29 = add i64 %28, 4
  %30 = call i64 @F71(ptr noundef %27, i64 noundef %29)
  %31 = load i64, ptr %5, align 8
  %32 = add i64 %30, %31
  ret i64 %32
}

; Function Attrs: nounwind
define dso_local i64 @F73(ptr noundef %0, i64 noundef %1) #0 {
  %3 = alloca ptr, align 8
  %4 = alloca i64, align 8
  %5 = alloca i64, align 8
  store ptr %0, ptr %3, align 8
  store i64 %1, ptr %4, align 8
  %6 = load ptr, ptr %3, align 8
  %7 = getelementptr inbounds %struct.CPU, ptr %6, i32 0, i32 0
  %8 = load i64, ptr %4, align 8
  %9 = lshr i64 %8, 2
  %10 = and i64 %9, 31
  %11 = getelementptr inbounds [32 x i64], ptr %7, i64 0, i64 %10
  %12 = load i64, ptr %11, align 8
  %13 = load i64, ptr %4, align 8
  %14 = add i64 %12, %13
  store i64 %14, ptr %5, align 8
  %15 = load i64, ptr %5, align 8
  %16 = mul i64 %15, 37
  %17 = load i64, ptr %5, align 8
  %18 = lshr i64 %17, 7
  %19 = xor i64 %16, %18
  store i64 %19, ptr %5, align 8
  %20 = load i64, ptr %5, align 8
  %21 = load ptr, ptr %3, align 8
  %22 = getelementptr inbounds %struct.CPU, ptr %21, i32 0, i32 0
  %23 = load i64, ptr %4, align 8
  %24 = lshr i64 %23, 2
  %25 = and i64 %24, 31
  %26 = getelementptr inbounds [32 x i64], ptr %22, i64 0, i64 %25
  store i64 %20, ptr %26, align 8
  %27 = load ptr, ptr %3, align 8
  %28 = load i64, ptr %4, align 8
  %29 = add i64 %28, 4
  %30 = call i64 @F72(ptr noundef %27, i64 noundef %29)
  %31 = load i64, ptr %5, align 8
  %32 = add i64 %30, %31
  ret i64 %32
}

; Function Attrs: nounwind
define dso_local i64 @F74(ptr noundef %0, i64 noundef %1) #0 {
  %3 = alloca ptr, align 8
  %4 = alloca i64, align 8
  %5 = alloca i64, align 8
  store ptr %0, ptr %3, align 8
  store i64 %1, ptr %4, align 8
  %6 = load ptr, ptr %3, align 8
  %7 = getelementptr inbounds %struct.CPU, ptr %6, i32 0, i32 0
  %8 = load i64, ptr %4, align 8
  %9 = lshr i64 %8, 2
  %10 = and i64 %9, 31
  %11 = getelementptr inbounds [32 x i64], ptr %7, i64 0, i64 %10
  %12 = load i64, ptr %11, align 8
  %13 = load i64, ptr %4, align 8
  %14 = add i64 %12, %13
  store i64 %14, ptr %5, align 8
  %15 = load i64, ptr %5, align 8
  %16 = mul i64 %15, 37
  %17 = load i64, ptr %5, align 8
  %18 = lshr i64 %17, 7
  %19 = xor i64 %16, %18
  store i64 %19, ptr %5, align 8
  %20 = load i64, ptr %5, align 8
  %21 = load ptr, ptr %3, align 8
  %22 = getelementptr inbounds %struct.CPU, ptr %21, i32 0, i32 0
  %23 = load i64, ptr %4, align 8
  %24 = lshr i64 %23, 2
  %25 = and i64 %24, 31
  %26 = getelementptr inbounds [32 x i64], ptr %22, i64 0, i64 %25
  store i64 %20, ptr %26, align 8
  %27 = load ptr, ptr %3, align 8
  %28 = load i64, ptr %4, align 8
  %29 = add i64 %28, 4
  %30 = call i64 @F73(ptr noundef %27, i64 noundef %29)
  %31 = load i64, ptr %5, align 8
  %32 = add i64 %30, %31
  ret i64 %32
}

; Function Attrs: nounwind
define dso_local i64 @F75(ptr noundef %0, i64 noundef %1) #0 {
  %3 = alloca ptr, align 8
  %4 = alloca i64, align 8
  %5 = alloca i64, align 8
  store ptr %0, ptr %3, align 8
  store i64 %1, ptr %4, align 8
  %6 = load ptr, ptr %3, align 8
  %7 = getelementptr inbounds %struct.CPU, ptr %6, i32 0, i32 0
  %8 = load i64, ptr %4, align 8
  %9 = lshr i64 %8, 2
  %10 = and i64 %9, 31
  %11 = getelementptr inbounds [32 x i64], ptr %7, i64 0, i64 %10
  %12 = load i64, ptr %11, align 8
  %13 = load i64, ptr %4, align 8
  %14 = add i64 %12, %13
  store i64 %14, ptr %5, align 8
  %15 = load i64, ptr %5, align 8
  %16 = mul i64 %15, 37
  %17 = load i64, ptr %5, align 8
  %18 = lshr i64 %17, 7
  %19 = xor i64 %16, %18
  store i64 %19, ptr %5, align 8
  %20 = load i64, ptr %5, align 8
  %21 = load ptr, ptr %3, align 8
  %22 = getelementptr inbounds %struct.CPU, ptr %21, i32 0, i32 0
  %23 = load i64, ptr %4, align 8
  %24 = lshr i64 %23, 2
  %25 = and i64 %24, 31
  %26 = getelementptr inbounds [32 x i64], ptr %22, i64 0, i64 %25
  store i64 %20, ptr %26, align 8
  %27 = load ptr, ptr %3, align 8
  %28 = load i64, ptr %4, align 8
  %29 = add i64 %28, 4
  %30 = call i64 @F74(ptr noundef %27, i64 noundef %29)
  %31 = load i64, ptr %5, align 8
  %32 = add i64 %30, %31
  ret i64 %32
}

; Function Attrs: nounwind
define dso_local i64 @F76(ptr noundef %0, i64 noundef %1) #0 {
  %3 = alloca ptr, align 8
  %4 = alloca i64, align 8
  %5 = alloca i64, align 8
  store ptr %0, ptr %3, align 8
  store i64 %1, ptr %4, align 8
  %6 = load ptr, ptr %3, align 8
  %7 = getelementptr inbounds %struct.CPU, ptr %6, i32 0, i32 0
  %8 = load i64, ptr %4, align 8
  %9 = lshr i64 %8, 2
  %10 = and i64 %9, 31
  %11 = getelementptr inbounds [32 x i64], ptr %7, i64 0, i64 %10
  %12 = load i64, ptr %11, align 8
  %13 = load i64, ptr %4, align 8
  %14 = add i64 %12, %13
  store i64 %14, ptr %5, align 8
  %15 = load i64, ptr %5, align 8
  %16 = mul i64 %15, 37
  %17 = load i64, ptr %5, align 8
  %18 = lshr i64 %17, 7
  %19 = xor i64 %16, %18
  store i64 %19, ptr %5, align 8
  %20 = load i64, ptr %5, align 8
  %21 = load ptr, ptr %3, align 8
  %22 = getelementptr inbounds %struct.CPU, ptr %21, i32 0, i32 0
  %23 = load i64, ptr %4, align 8
  %24 = lshr i64 %23, 2
  %25 = and i64 %24, 31
  %26 = getelementptr inbounds [32 x i64], ptr %22, i64 0, i64 %25
  store i64 %20, ptr %26, align 8
  %27 = load ptr, ptr %3, align 8
  %28 = load i64, ptr %4, align 8
  %29 = add i64 %28, 4
  %30 = call i64 @F75(ptr noundef %27, i64 noundef %29)
  %31 = load i64, ptr %5, align 8
  %32 = add i64 %30, %31
  ret i64 %32
}

; Function Attrs: nounwind
define dso_local i64 @F77(ptr noundef %0, i64 noundef %1) #0 {
  %3 = alloca ptr, align 8
  %4 = alloca i64, align 8
  %5 = alloca i64, align 8
  store ptr %0, ptr %3, align 8
  store i64 %1, ptr %4, align 8
  %6 = load ptr, ptr %3, align 8
  %7 = getelementptr inbounds %struct.CPU, ptr %6, i32 0, i32 0
  %8 = load i64, ptr %4, align 8
  %9 = lshr i64 %8, 2
  %10 = and i64 %9, 31
  %11 = getelementptr inbounds [32 x i64], ptr %7, i64 0, i64 %10
  %12 = load i64, ptr %11, align 8
  %13 = load i64, ptr %4, align 8
  %14 = add i64 %12, %13
  store i64 %14, ptr %5, align 8
  %15 = load i64, ptr %5, align 8
  %16 = mul i64 %15, 37
  %17 = load i64, ptr %5, align 8
  %18 = lshr i64 %17, 7
  %19 = xor i64 %16, %18
  store i64 %19, ptr %5, align 8
  %20 = load i64, ptr %5, align 8
  %21 = load ptr, ptr %3, align 8
  %22 = getelementptr inbounds %struct.CPU, ptr %21, i32 0, i32 0
  %23 = load i64, ptr %4, align 8
  %24 = lshr i64 %23, 2
  %25 = and i64 %24, 31
  %26 = getelementptr inbounds [32 x i64], ptr %22, i64 0, i64 %25
  store i64 %20, ptr %26, align 8
  %27 = load ptr, ptr %3, align 8
  %28 = load i64, ptr %4, align 8
  %29 = add i64 %28, 4
  %30 = call i64 @F76(ptr noundef %27, i64 noundef %29)
  %31 = load i64, ptr %5, align 8
  %32 = add i64 %30, %31
  ret i64 %32
}

; Function Attrs: nounwind
define dso_local i64 @F78(ptr noundef %0, i64 noundef %1) #0 {
  %3 = alloca ptr, align 8
  %4 = alloca i64, align 8
  %5 = alloca i64, align 8
  store ptr %0, ptr %3, align 8
  store i64 %1, ptr %4, align 8
  %6 = load ptr, ptr %3, align 8
  %7 = getelementptr inbounds %struct.CPU, ptr %6, i32 0, i32 0
  %8 = load i64, ptr %4, align 8
  %9 = lshr i64 %8, 2
  %10 = and i64 %9, 31
  %11 = getelementptr inbounds [32 x i64], ptr %7, i64 0, i64 %10
  %12 = load i64, ptr %11, align 8
  %13 = load i64, ptr %4, align 8
  %14 = add i64 %12, %13
  store i64 %14, ptr %5, align 8
  %15 = load i64, ptr %5, align 8
  %16 = mul i64 %15, 37
  %17 = load i64, ptr %5, align 8
  %18 = lshr i64 %17, 7
  %19 = xor i64 %16, %18
  store i64 %19, ptr %5, align 8
  %20 = load i64, ptr %5, align 8
  %21 = load ptr, ptr %3, align 8
  %22 = getelementptr inbounds %struct.CPU, ptr %21, i32 0, i32 0
  %23 = load i64, ptr %4, align 8
  %24 = lshr i64 %23, 2
  %25 = and i64 %24, 31
  %26 = getelementptr inbounds [32 x i64], ptr %22, i64 0, i64 %25
  store i64 %20, ptr %26, align 8
  %27 = load ptr, ptr %3, align 8
  %28 = load i64, ptr %4, align 8
  %29 = add i64 %28, 4
  %30 = call i64 @F77(ptr noundef %27, i64 noundef %29)
  %31 = load i64, ptr %5, align 8
  %32 = add i64 %30, %31
  ret i64 %32
}

; Function Attrs: nounwind
define dso_local i64 @F79(ptr noundef %0, i64 noundef %1) #0 {
  %3 = alloca ptr, align 8
  %4 = alloca i64, align 8
  %5 = alloca i64, align 8
  store ptr %0, ptr %3, align 8
  store i64 %1, ptr %4, align 8
  %6 = load ptr, ptr %3, align 8
  %7 = getelementptr inbounds %struct.CPU, ptr %6, i32 0, i32 0
  %8 = load i64, ptr %4, align 8
  %9 = lshr i64 %8, 2
  %10 = and i64 %9, 31
  %11 = getelementptr inbounds [32 x i64], ptr %7, i64 0, i64 %10
  %12 = load i64, ptr %11, align 8
  %13 = load i64, ptr %4, align 8
  %14 = add i64 %12, %13
  store i64 %14, ptr %5, align 8
  %15 = load i64, ptr %5, align 8
  %16 = mul i64 %15, 37
  %17 = load i64, ptr %5, align 8
  %18 = lshr i64 %17, 7
  %19 = xor i64 %16, %18
  store i64 %19, ptr %5, align 8
  %20 = load i64, ptr %5, align 8
  %21 = load ptr, ptr %3, align 8
  %22 = getelementptr inbounds %struct.CPU, ptr %21, i32 0, i32 0
  %23 = load i64, ptr %4, align 8
  %24 = lshr i64 %23, 2
  %25 = and i64 %24, 31
  %26 = getelementptr inbounds [32 x i64], ptr %22, i64 0, i64 %25
  store i64 %20, ptr %26, align 8
  %27 = load ptr, ptr %3, align 8
  %28 = load i64, ptr %4, align 8
  %29 = add i64 %28, 4
  %30 = call i64 @F78(ptr noundef %27, i64 noundef %29)
  %31 = load i64, ptr %5, align 8
  %32 = add i64 %30, %31
  ret i64 %32
}

; Function Attrs: nounwind
define dso_local i64 @F80(ptr noundef %0, i64 noundef %1) #0 {
  %3 = alloca ptr, align 8
  %4 = alloca i64, align 8
  %5 = alloca i64, align 8
  store ptr %0, ptr %3, align 8
  store i64 %1, ptr %4, align 8
  %6 = load ptr, ptr %3, align 8
  %7 = getelementptr inbounds %struct.CPU, ptr %6, i32 0, i32 0
  %8 = load i64, ptr %4, align 8
  %9 = lshr i64 %8, 2
  %10 = and i64 %9, 31
  %11 = getelementptr inbounds [32 x i64], ptr %7, i64 0, i64 %10
  %12 = load i64, ptr %11, align 8
  %13 = load i64, ptr %4, align 8
  %14 = add i64 %12, %13
  store i64 %14, ptr %5, align 8
  %15 = load i64, ptr %5, align 8
  %16 = mul i64 %15, 37
  %17 = load i64, ptr %5, align 8
  %18 = lshr i64 %17, 7
  %19 = xor i64 %16, %18
  store i64 %19, ptr %5, align 8
  %20 = load i64, ptr %5, align 8
  %21 = load ptr, ptr %3, align 8
  %22 = getelementptr inbounds %struct.CPU, ptr %21, i32 0, i32 0
  %23 = load i64, ptr %4, align 8
  %24 = lshr i64 %23, 2
  %25 = and i64 %24, 31
  %26 = getelementptr inbounds [32 x i64], ptr %22, i64 0, i64 %25
  store i64 %20, ptr %26, align 8
  %27 = load ptr, ptr %3, align 8
  %28 = load i64, ptr %4, align 8
  %29 = add i64 %28, 4
  %30 = call i64 @F79(ptr noundef %27, i64 noundef %29)
  %31 = load i64, ptr %5, align 8
  %32 = add i64 %30, %31
  ret i64 %32
}

; Function Attrs: nounwind
define dso_local i64 @F81(ptr noundef %0, i64 noundef %1) #0 {
  %3 = alloca ptr, align 8
  %4 = alloca i64, align 8
  %5 = alloca i64, align 8
  store ptr %0, ptr %3, align 8
  store i64 %1, ptr %4, align 8
  %6 = load ptr, ptr %3, align 8
  %7 = getelementptr inbounds %struct.CPU, ptr %6, i32 0, i32 0
  %8 = load i64, ptr %4, align 8
  %9 = lshr i64 %8, 2
  %10 = and i64 %9, 31
  %11 = getelementptr inbounds [32 x i64], ptr %7, i64 0, i64 %10
  %12 = load i64, ptr %11, align 8
  %13 = load i64, ptr %4, align 8
  %14 = add i64 %12, %13
  store i64 %14, ptr %5, align 8
  %15 = load i64, ptr %5, align 8
  %16 = mul i64 %15, 37
  %17 = load i64, ptr %5, align 8
  %18 = lshr i64 %17, 7
  %19 = xor i64 %16, %18
  store i64 %19, ptr %5, align 8
  %20 = load i64, ptr %5, align 8
  %21 = load ptr, ptr %3, align 8
  %22 = getelementptr inbounds %struct.CPU, ptr %21, i32 0, i32 0
  %23 = load i64, ptr %4, align 8
  %24 = lshr i64 %23, 2
  %25 = and i64 %24, 31
  %26 = getelementptr inbounds [32 x i64], ptr %22, i64 0, i64 %25
  store i64 %20, ptr %26, align 8
  %27 = load ptr, ptr %3, align 8
  %28 = load i64, ptr %4, align 8
  %29 = add i64 %28, 4
  %30 = call i64 @F80(ptr noundef %27, i64 noundef %29)
  %31 = load i64, ptr %5, align 8
  %32 = add i64 %30, %31
  ret i64 %32
}

; Function Attrs: nounwind
define dso_local i64 @F82(ptr noundef %0, i64 noundef %1) #0 {
  %3 = alloca ptr, align 8
  %4 = alloca i64, align 8
  %5 = alloca i64, align 8
  store ptr %0, ptr %3, align 8
  store i64 %1, ptr %4, align 8
  %6 = load ptr, ptr %3, align 8
  %7 = getelementptr inbounds %struct.CPU, ptr %6, i32 0, i32 0
  %8 = load i64, ptr %4, align 8
  %9 = lshr i64 %8, 2
  %10 = and i64 %9, 31
  %11 = getelementptr inbounds [32 x i64], ptr %7, i64 0, i64 %10
  %12 = load i64, ptr %11, align 8
  %13 = load i64, ptr %4, align 8
  %14 = add i64 %12, %13
  store i64 %14, ptr %5, align 8
  %15 = load i64, ptr %5, align 8
  %16 = mul i64 %15, 37
  %17 = load i64, ptr %5, align 8
  %18 = lshr i64 %17, 7
  %19 = xor i64 %16, %18
  store i64 %19, ptr %5, align 8
  %20 = load i64, ptr %5, align 8
  %21 = load ptr, ptr %3, align 8
  %22 = getelementptr inbounds %struct.CPU, ptr %21, i32 0, i32 0
  %23 = load i64, ptr %4, align 8
  %24 = lshr i64 %23, 2
  %25 = and i64 %24, 31
  %26 = getelementptr inbounds [32 x i64], ptr %22, i64 0, i64 %25
  store i64 %20, ptr %26, align 8
  %27 = load ptr, ptr %3, align 8
  %28 = load i64, ptr %4, align 8
  %29 = add i64 %28, 4
  %30 = call i64 @F81(ptr noundef %27, i64 noundef %29)
  %31 = load i64, ptr %5, align 8
  %32 = add i64 %30, %31
  ret i64 %32
}

; Function Attrs: nounwind
define dso_local i64 @F83(ptr noundef %0, i64 noundef %1) #0 {
  %3 = alloca ptr, align 8
  %4 = alloca i64, align 8
  %5 = alloca i64, align 8
  store ptr %0, ptr %3, align 8
  store i64 %1, ptr %4, align 8
  %6 = load ptr, ptr %3, align 8
  %7 = getelementptr inbounds %struct.CPU, ptr %6, i32 0, i32 0
  %8 = load i64, ptr %4, align 8
  %9 = lshr i64 %8, 2
  %10 = and i64 %9, 31
  %11 = getelementptr inbounds [32 x i64], ptr %7, i64 0, i64 %10
  %12 = load i64, ptr %11, align 8
  %13 = load i64, ptr %4, align 8
  %14 = add i64 %12, %13
  store i64 %14, ptr %5, align 8
  %15 = load i64, ptr %5, align 8
  %16 = mul i64 %15, 37
  %17 = load i64, ptr %5, align 8
  %18 = lshr i64 %17, 7
  %19 = xor i64 %16, %18
  store i64 %19, ptr %5, align 8
  %20 = load i64, ptr %5, align 8
  %21 = load ptr, ptr %3, align 8
  %22 = getelementptr inbounds %struct.CPU, ptr %21, i32 0, i32 0
  %23 = load i64, ptr %4, align 8
  %24 = lshr i64 %23, 2
  %25 = and i64 %24, 31
  %26 = getelementptr inbounds [32 x i64], ptr %22, i64 0, i64 %25
  store i64 %20, ptr %26, align 8
  %27 = load ptr, ptr %3, align 8
  %28 = load i64, ptr %4, align 8
  %29 = add i64 %28, 4
  %30 = call i64 @F82(ptr noundef %27, i64 noundef %29)
  %31 = load i64, ptr %5, align 8
  %32 = add i64 %30, %31
  ret i64 %32
}

; Function Attrs: nounwind
define dso_local i64 @F84(ptr noundef %0, i64 noundef %1) #0 {
  %3 = alloca ptr, align 8
  %4 = alloca i64, align 8
  %5 = alloca i64, align 8
  store ptr %0, ptr %3, align 8
  store i64 %1, ptr %4, align 8
  %6 = load ptr, ptr %3, align 8
  %7 = getelementptr inbounds %struct.CPU, ptr %6, i32 0, i32 0
  %8 = load i64, ptr %4, align 8
  %9 = lshr i64 %8, 2
  %10 = and i64 %9, 31
  %11 = getelementptr inbounds [32 x i64], ptr %7, i64 0, i64 %10
  %12 = load i64, ptr %11, align 8
  %13 = load i64, ptr %4, align 8
  %14 = add i64 %12, %13
  store i64 %14, ptr %5, align 8
  %15 = load i64, ptr %5, align 8
  %16 = mul i64 %15, 37
  %17 = load i64, ptr %5, align 8
  %18 = lshr i64 %17, 7
  %19 = xor i64 %16, %18
  store i64 %19, ptr %5, align 8
  %20 = load i64, ptr %5, align 8
  %21 = load ptr, ptr %3, align 8
  %22 = getelementptr inbounds %struct.CPU, ptr %21, i32 0, i32 0
  %23 = load i64, ptr %4, align 8
  %24 = lshr i64 %23, 2
  %25 = and i64 %24, 31
  %26 = getelementptr inbounds [32 x i64], ptr %22, i64 0, i64 %25
  store i64 %20, ptr %26, align 8
  %27 = load ptr, ptr %3, align 8
  %28 = load i64, ptr %4, align 8
  %29 = add i64 %28, 4
  %30 = call i64 @F83(ptr noundef %27, i64 noundef %29)
  %31 = load i64, ptr %5, align 8
  %32 = add i64 %30, %31
  ret i64 %32
}

; Function Attrs: nounwind
define dso_local i64 @F85(ptr noundef %0, i64 noundef %1) #0 {
  %3 = alloca ptr, align 8
  %4 = alloca i64, align 8
  %5 = alloca i64, align 8
  store ptr %0, ptr %3, align 8
  store i64 %1, ptr %4, align 8
  %6 = load ptr, ptr %3, align 8
  %7 = getelementptr inbounds %struct.CPU, ptr %6, i32 0, i32 0
  %8 = load i64, ptr %4, align 8
  %9 = lshr i64 %8, 2
  %10 = and i64 %9, 31
  %11 = getelementptr inbounds [32 x i64], ptr %7, i64 0, i64 %10
  %12 = load i64, ptr %11, align 8
  %13 = load i64, ptr %4, align 8
  %14 = add i64 %12, %13
  store i64 %14, ptr %5, align 8
  %15 = load i64, ptr %5, align 8
  %16 = mul i64 %15, 37
  %17 = load i64, ptr %5, align 8
  %18 = lshr i64 %17, 7
  %19 = xor i64 %16, %18
  store i64 %19, ptr %5, align 8
  %20 = load i64, ptr %5, align 8
  %21 = load ptr, ptr %3, align 8
  %22 = getelementptr inbounds %struct.CPU, ptr %21, i32 0, i32 0
  %23 = load i64, ptr %4, align 8
  %24 = lshr i64 %23, 2
  %25 = and i64 %24, 31
  %26 = getelementptr inbounds [32 x i64], ptr %22, i64 0, i64 %25
  store i64 %20, ptr %26, align 8
  %27 = load ptr, ptr %3, align 8
  %28 = load i64, ptr %4, align 8
  %29 = add i64 %28, 4
  %30 = call i64 @F84(ptr noundef %27, i64 noundef %29)
  %31 = load i64, ptr %5, align 8
  %32 = add i64 %30, %31
  ret i64 %32
}

; Function Attrs: nounwind
define dso_local i64 @F86(ptr noundef %0, i64 noundef %1) #0 {
  %3 = alloca ptr, align 8
  %4 = alloca i64, align 8
  %5 = alloca i64, align 8
  store ptr %0, ptr %3, align 8
  store i64 %1, ptr %4, align 8
  %6 = load ptr, ptr %3, align 8
  %7 = getelementptr inbounds %struct.CPU, ptr %6, i32 0, i32 0
  %8 = load i64, ptr %4, align 8
  %9 = lshr i64 %8, 2
  %10 = and i64 %9, 31
  %11 = getelementptr inbounds [32 x i64], ptr %7, i64 0, i64 %10
  %12 = load i64, ptr %11, align 8
  %13 = load i64, ptr %4, align 8
  %14 = add i64 %12, %13
  store i64 %14, ptr %5, align 8
  %15 = load i64, ptr %5, align 8
  %16 = mul i64 %15, 37
  %17 = load i64, ptr %5, align 8
  %18 = lshr i64 %17, 7
  %19 = xor i64 %16, %18
  store i64 %19, ptr %5, align 8
  %20 = load i64, ptr %5, align 8
  %21 = load ptr, ptr %3, align 8
  %22 = getelementptr inbounds %struct.CPU, ptr %21, i32 0, i32 0
  %23 = load i64, ptr %4, align 8
  %24 = lshr i64 %23, 2
  %25 = and i64 %24, 31
  %26 = getelementptr inbounds [32 x i64], ptr %22, i64 0, i64 %25
  store i64 %20, ptr %26, align 8
  %27 = load ptr, ptr %3, align 8
  %28 = load i64, ptr %4, align 8
  %29 = add i64 %28, 4
  %30 = call i64 @F85(ptr noundef %27, i64 noundef %29)
  %31 = load i64, ptr %5, align 8
  %32 = add i64 %30, %31
  ret i64 %32
}

; Function Attrs: nounwind
define dso_local i64 @F87(ptr noundef %0, i64 noundef %1) #0 {
  %3 = alloca ptr, align 8
  %4 = alloca i64, align 8
  %5 = alloca i64, align 8
  store ptr %0, ptr %3, align 8
  store i64 %1, ptr %4, align 8
  %6 = load ptr, ptr %3, align 8
  %7 = getelementptr inbounds %struct.CPU, ptr %6, i32 0, i32 0
  %8 = load i64, ptr %4, align 8
  %9 = lshr i64 %8, 2
  %10 = and i64 %9, 31
  %11 = getelementptr inbounds [32 x i64], ptr %7, i64 0, i64 %10
  %12 = load i64, ptr %11, align 8
  %13 = load i64, ptr %4, align 8
  %14 = add i64 %12, %13
  store i64 %14, ptr %5, align 8
  %15 = load i64, ptr %5, align 8
  %16 = mul i64 %15, 37
  %17 = load i64, ptr %5, align 8
  %18 = lshr i64 %17, 7
  %19 = xor i64 %16, %18
  store i64 %19, ptr %5, align 8
  %20 = load i64, ptr %5, align 8
  %21 = load ptr, ptr %3, align 8
  %22 = getelementptr inbounds %struct.CPU, ptr %21, i32 0, i32 0
  %23 = load i64, ptr %4, align 8
  %24 = lshr i64 %23, 2
  %25 = and i64 %24, 31
  %26 = getelementptr inbounds [32 x i64], ptr %22, i64 0, i64 %25
  store i64 %20, ptr %26, align 8
  %27 = load ptr, ptr %3, align 8
  %28 = load i64, ptr %4, align 8
  %29 = add i64 %28, 4
  %30 = call i64 @F86(ptr noundef %27, i64 noundef %29)
  %31 = load i64, ptr %5, align 8
  %32 = add i64 %30, %31
  ret i64 %32
}

; Function Attrs: nounwind
define dso_local i64 @F88(ptr noundef %0, i64 noundef %1) #0 {
  %3 = alloca ptr, align 8
  %4 = alloca i64, align 8
  %5 = alloca i64, align 8
  store ptr %0, ptr %3, align 8
  store i64 %1, ptr %4, align 8
  %6 = load ptr, ptr %3, align 8
  %7 = getelementptr inbounds %struct.CPU, ptr %6, i32 0, i32 0
  %8 = load i64, ptr %4, align 8
  %9 = lshr i64 %8, 2
  %10 = and i64 %9, 31
  %11 = getelementptr inbounds [32 x i64], ptr %7, i64 0, i64 %10
  %12 = load i64, ptr %11, align 8
  %13 = load i64, ptr %4, align 8
  %14 = add i64 %12, %13
  store i64 %14, ptr %5, align 8
  %15 = load i64, ptr %5, align 8
  %16 = mul i64 %15, 37
  %17 = load i64, ptr %5, align 8
  %18 = lshr i64 %17, 7
  %19 = xor i64 %16, %18
  store i64 %19, ptr %5, align 8
  %20 = load i64, ptr %5, align 8
  %21 = load ptr, ptr %3, align 8
  %22 = getelementptr inbounds %struct.CPU, ptr %21, i32 0, i32 0
  %23 = load i64, ptr %4, align 8
  %24 = lshr i64 %23, 2
  %25 = and i64 %24, 31
  %26 = getelementptr inbounds [32 x i64], ptr %22, i64 0, i64 %25
  store i64 %20, ptr %26, align 8
  %27 = load ptr, ptr %3, align 8
  %28 = load i64, ptr %4, align 8
  %29 = add i64 %28, 4
  %30 = call i64 @F87(ptr noundef %27, i64 noundef %29)
  %31 = load i64, ptr %5, align 8
  %32 = add i64 %30, %31
  ret i64 %32
}

; Function Attrs: nounwind
define dso_local i64 @F89(ptr noundef %0, i64 noundef %1) #0 {
  %3 = alloca ptr, align 8
  %4 = alloca i64, align 8
  %5 = alloca i64, align 8
  store ptr %0, ptr %3, align 8
  store i64 %1, ptr %4, align 8
  %6 = load ptr, ptr %3, align 8
  %7 = getelementptr inbounds %struct.CPU, ptr %6, i32 0, i32 0
  %8 = load i64, ptr %4, align 8
  %9 = lshr i64 %8, 2
  %10 = and i64 %9, 31
  %11 = getelementptr inbounds [32 x i64], ptr %7, i64 0, i64 %10
  %12 = load i64, ptr %11, align 8
  %13 = load i64, ptr %4, align 8
  %14 = add i64 %12, %13
  store i64 %14, ptr %5, align 8
  %15 = load i64, ptr %5, align 8
  %16 = mul i64 %15, 37
  %17 = load i64, ptr %5, align 8
  %18 = lshr i64 %17, 7
  %19 = xor i64 %16, %18
  store i64 %19, ptr %5, align 8
  %20 = load i64, ptr %5, align 8
  %21 = load ptr, ptr %3, align 8
  %22 = getelementptr inbounds %struct.CPU, ptr %21, i32 0, i32 0
  %23 = load i64, ptr %4, align 8
  %24 = lshr i64 %23, 2
  %25 = and i64 %24, 31
  %26 = getelementptr inbounds [32 x i64], ptr %22, i64 0, i64 %25
  store i64 %20, ptr %26, align 8
  %27 = load ptr, ptr %3, align 8
  %28 = load i64, ptr %4, align 8
  %29 = add i64 %28, 4
  %30 = call i64 @F88(ptr noundef %27, i64 noundef %29)
  %31 = load i64, ptr %5, align 8
  %32 = add i64 %30, %31
  ret i64 %32
}

; Function Attrs: nounwind
define dso_local i64 @F90(ptr noundef %0, i64 noundef %1) #0 {
  %3 = alloca ptr, align 8
  %4 = alloca i64, align 8
  %5 = alloca i64, align 8
  store ptr %0, ptr %3, align 8
  store i64 %1, ptr %4, align 8
  %6 = load ptr, ptr %3, align 8
  %7 = getelementptr inbounds %struct.CPU, ptr %6, i32 0, i32 0
  %8 = load i64, ptr %4, align 8
  %9 = lshr i64 %8, 2
  %10 = and i64 %9, 31
  %11 = getelementptr inbounds [32 x i64], ptr %7, i64 0, i64 %10
  %12 = load i64, ptr %11, align 8
  %13 = load i64, ptr %4, align 8
  %14 = add i64 %12, %13
  store i64 %14, ptr %5, align 8
  %15 = load i64, ptr %5, align 8
  %16 = mul i64 %15, 37
  %17 = load i64, ptr %5, align 8
  %18 = lshr i64 %17, 7
  %19 = xor i64 %16, %18
  store i64 %19, ptr %5, align 8
  %20 = load i64, ptr %5, align 8
  %21 = load ptr, ptr %3, align 8
  %22 = getelementptr inbounds %struct.CPU, ptr %21, i32 0, i32 0
  %23 = load i64, ptr %4, align 8
  %24 = lshr i64 %23, 2
  %25 = and i64 %24, 31
  %26 = getelementptr inbounds [32 x i64], ptr %22, i64 0, i64 %25
  store i64 %20, ptr %26, align 8
  %27 = load ptr, ptr %3, align 8
  %28 = load i64, ptr %4, align 8
  %29 = add i64 %28, 4
  %30 = call i64 @F89(ptr noundef %27, i64 noundef %29)
  %31 = load i64, ptr %5, align 8
  %32 = add i64 %30, %31
  ret i64 %32
}

; Function Attrs: nounwind
define dso_local i64 @F91(ptr noundef %0, i64 noundef %1) #0 {
  %3 = alloca ptr, align 8
  %4 = alloca i64, align 8
  %5 = alloca i64, align 8
  store ptr %0, ptr %3, align 8
  store i64 %1, ptr %4, align 8
  %6 = load ptr, ptr %3, align 8
  %7 = getelementptr inbounds %struct.CPU, ptr %6, i32 0, i32 0
  %8 = load i64, ptr %4, align 8
  %9 = lshr i64 %8, 2
  %10 = and i64 %9, 31
  %11 = getelementptr inbounds [32 x i64], ptr %7, i64 0, i64 %10
  %12 = load i64, ptr %11, align 8
  %13 = load i64, ptr %4, align 8
  %14 = add i64 %12, %13
  store i64 %14, ptr %5, align 8
  %15 = load i64, ptr %5, align 8
  %16 = mul i64 %15, 37
  %17 = load i64, ptr %5, align 8
  %18 = lshr i64 %17, 7
  %19 = xor i64 %16, %18
  store i64 %19, ptr %5, align 8
  %20 = load i64, ptr %5, align 8
  %21 = load ptr, ptr %3, align 8
  %22 = getelementptr inbounds %struct.CPU, ptr %21, i32 0, i32 0
  %23 = load i64, ptr %4, align 8
  %24 = lshr i64 %23, 2
  %25 = and i64 %24, 31
  %26 = getelementptr inbounds [32 x i64], ptr %22, i64 0, i64 %25
  store i64 %20, ptr %26, align 8
  %27 = load ptr, ptr %3, align 8
  %28 = load i64, ptr %4, align 8
  %29 = add i64 %28, 4
  %30 = call i64 @F90(ptr noundef %27, i64 noundef %29)
  %31 = load i64, ptr %5, align 8
  %32 = add i64 %30, %31
  ret i64 %32
}

; Function Attrs: nounwind
define dso_local i64 @F92(ptr noundef %0, i64 noundef %1) #0 {
  %3 = alloca ptr, align 8
  %4 = alloca i64, align 8
  %5 = alloca i64, align 8
  store ptr %0, ptr %3, align 8
  store i64 %1, ptr %4, align 8
  %6 = load ptr, ptr %3, align 8
  %7 = getelementptr inbounds %struct.CPU, ptr %6, i32 0, i32 0
  %8 = load i64, ptr %4, align 8
  %9 = lshr i64 %8, 2
  %10 = and i64 %9, 31
  %11 = getelementptr inbounds [32 x i64], ptr %7, i64 0, i64 %10
  %12 = load i64, ptr %11, align 8
  %13 = load i64, ptr %4, align 8
  %14 = add i64 %12, %13
  store i64 %14, ptr %5, align 8
  %15 = load i64, ptr %5, align 8
  %16 = mul i64 %15, 37
  %17 = load i64, ptr %5, align 8
  %18 = lshr i64 %17, 7
  %19 = xor i64 %16, %18
  store i64 %19, ptr %5, align 8
  %20 = load i64, ptr %5, align 8
  %21 = load ptr, ptr %3, align 8
  %22 = getelementptr inbounds %struct.CPU, ptr %21, i32 0, i32 0
  %23 = load i64, ptr %4, align 8
  %24 = lshr i64 %23, 2
  %25 = and i64 %24, 31
  %26 = getelementptr inbounds [32 x i64], ptr %22, i64 0, i64 %25
  store i64 %20, ptr %26, align 8
  %27 = load ptr, ptr %3, align 8
  %28 = load i64, ptr %4, align 8
  %29 = add i64 %28, 4
  %30 = call i64 @F91(ptr noundef %27, i64 noundef %29)
  %31 = load i64, ptr %5, align 8
  %32 = add i64 %30, %31
  ret i64 %32
}

; Function Attrs: nounwind
define dso_local i64 @F93(ptr noundef %0, i64 noundef %1) #0 {
  %3 = alloca ptr, align 8
  %4 = alloca i64, align 8
  %5 = alloca i64, align 8
  store ptr %0, ptr %3, align 8
  store i64 %1, ptr %4, align 8
  %6 = load ptr, ptr %3, align 8
  %7 = getelementptr inbounds %struct.CPU, ptr %6, i32 0, i32 0
  %8 = load i64, ptr %4, align 8
  %9 = lshr i64 %8, 2
  %10 = and i64 %9, 31
  %11 = getelementptr inbounds [32 x i64], ptr %7, i64 0, i64 %10
  %12 = load i64, ptr %11, align 8
  %13 = load i64, ptr %4, align 8
  %14 = add i64 %12, %13
  store i64 %14, ptr %5, align 8
  %15 = load i64, ptr %5, align 8
  %16 = mul i64 %15, 37
  %17 = load i64, ptr %5, align 8
  %18 = lshr i64 %17, 7
  %19 = xor i64 %16, %18
  store i64 %19, ptr %5, align 8
  %20 = load i64, ptr %5, align 8
  %21 = load ptr, ptr %3, align 8
  %22 = getelementptr inbounds %struct.CPU, ptr %21, i32 0, i32 0
  %23 = load i64, ptr %4, align 8
  %24 = lshr i64 %23, 2
  %25 = and i64 %24, 31
  %26 = getelementptr inbounds [32 x i64], ptr %22, i64 0, i64 %25
  store i64 %20, ptr %26, align 8
  %27 = load ptr, ptr %3, align 8
  %28 = load i64, ptr %4, align 8
  %29 = add i64 %28, 4
  %30 = call i64 @F92(ptr noundef %27, i64 noundef %29)
  %31 = load i64, ptr %5, align 8
  %32 = add i64 %30, %31
  ret i64 %32
}

; Function Attrs: nounwind
define dso_local i64 @F94(ptr noundef %0, i64 noundef %1) #0 {
  %3 = alloca ptr, align 8
  %4 = alloca i64, align 8
  %5 = alloca i64, align 8
  store ptr %0, ptr %3, align 8
  store i64 %1, ptr %4, align 8
  %6 = load ptr, ptr %3, align 8
  %7 = getelementptr inbounds %struct.CPU, ptr %6, i32 0, i32 0
  %8 = load i64, ptr %4, align 8
  %9 = lshr i64 %8, 2
  %10 = and i64 %9, 31
  %11 = getelementptr inbounds [32 x i64], ptr %7, i64 0, i64 %10
  %12 = load i64, ptr %11, align 8
  %13 = load i64, ptr %4, align 8
  %14 = add i64 %12, %13
  store i64 %14, ptr %5, align 8
  %15 = load i64, ptr %5, align 8
  %16 = mul i64 %15, 37
  %17 = load i64, ptr %5, align 8
  %18 = lshr i64 %17, 7
  %19 = xor i64 %16, %18
  store i64 %19, ptr %5, align 8
  %20 = load i64, ptr %5, align 8
  %21 = load ptr, ptr %3, align 8
  %22 = getelementptr inbounds %struct.CPU, ptr %21, i32 0, i32 0
  %23 = load i64, ptr %4, align 8
  %24 = lshr i64 %23, 2
  %25 = and i64 %24, 31
  %26 = getelementptr inbounds [32 x i64], ptr %22, i64 0, i64 %25
  store i64 %20, ptr %26, align 8
  %27 = load ptr, ptr %3, align 8
  %28 = load i64, ptr %4, align 8
  %29 = add i64 %28, 4
  %30 = call i64 @F93(ptr noundef %27, i64 noundef %29)
  %31 = load i64, ptr %5, align 8
  %32 = add i64 %30, %31
  ret i64 %32
}

; Function Attrs: nounwind
define dso_local i64 @F95(ptr noundef %0, i64 noundef %1) #0 {
  %3 = alloca ptr, align 8
  %4 = alloca i64, align 8
  %5 = alloca i64, align 8
  store ptr %0, ptr %3, align 8
  store i64 %1, ptr %4, align 8
  %6 = load ptr, ptr %3, align 8
  %7 = getelementptr inbounds %struct.CPU, ptr %6, i32 0, i32 0
  %8 = load i64, ptr %4, align 8
  %9 = lshr i64 %8, 2
  %10 = and i64 %9, 31
  %11 = getelementptr inbounds [32 x i64], ptr %7, i64 0, i64 %10
  %12 = load i64, ptr %11, align 8
  %13 = load i64, ptr %4, align 8
  %14 = add i64 %12, %13
  store i64 %14, ptr %5, align 8
  %15 = load i64, ptr %5, align 8
  %16 = mul i64 %15, 37
  %17 = load i64, ptr %5, align 8
  %18 = lshr i64 %17, 7
  %19 = xor i64 %16, %18
  store i64 %19, ptr %5, align 8
  %20 = load i64, ptr %5, align 8
  %21 = load ptr, ptr %3, align 8
  %22 = getelementptr inbounds %struct.CPU, ptr %21, i32 0, i32 0
  %23 = load i64, ptr %4, align 8
  %24 = lshr i64 %23, 2
  %25 = and i64 %24, 31
  %26 = getelementptr inbounds [32 x i64], ptr %22, i64 0, i64 %25
  store i64 %20, ptr %26, align 8
  %27 = load ptr, ptr %3, align 8
  %28 = load i64, ptr %4, align 8
  %29 = add i64 %28, 4
  %30 = call i64 @F94(ptr noundef %27, i64 noundef %29)
  %31 = load i64, ptr %5, align 8
  %32 = add i64 %30, %31
  ret i64 %32
}

; Function Attrs: nounwind
define dso_local i64 @F96(ptr noundef %0, i64 noundef %1) #0 {
  %3 = alloca ptr, align 8
  %4 = alloca i64, align 8
  %5 = alloca i64, align 8
  store ptr %0, ptr %3, align 8
  store i64 %1, ptr %4, align 8
  %6 = load ptr, ptr %3, align 8
  %7 = getelementptr inbounds %struct.CPU, ptr %6, i32 0, i32 0
  %8 = load i64, ptr %4, align 8
  %9 = lshr i64 %8, 2
  %10 = and i64 %9, 31
  %11 = getelementptr inbounds [32 x i64], ptr %7, i64 0, i64 %10
  %12 = load i64, ptr %11, align 8
  %13 = load i64, ptr %4, align 8
  %14 = add i64 %12, %13
  store i64 %14, ptr %5, align 8
  %15 = load i64, ptr %5, align 8
  %16 = mul i64 %15, 37
  %17 = load i64, ptr %5, align 8
  %18 = lshr i64 %17, 7
  %19 = xor i64 %16, %18
  store i64 %19, ptr %5, align 8
  %20 = load i64, ptr %5, align 8
  %21 = load ptr, ptr %3, align 8
  %22 = getelementptr inbounds %struct.CPU, ptr %21, i32 0, i32 0
  %23 = load i64, ptr %4, align 8
  %24 = lshr i64 %23, 2
  %25 = and i64 %24, 31
  %26 = getelementptr inbounds [32 x i64], ptr %22, i64 0, i64 %25
  store i64 %20, ptr %26, align 8
  %27 = load ptr, ptr %3, align 8
  %28 = load i64, ptr %4, align 8
  %29 = add i64 %28, 4
  %30 = call i64 @F95(ptr noundef %27, i64 noundef %29)
  %31 = load i64, ptr %5, align 8
  %32 = add i64 %30, %31
  ret i64 %32
}

; Function Attrs: nounwind
define dso_local i64 @F97(ptr noundef %0, i64 noundef %1) #0 {
  %3 = alloca ptr, align 8
  %4 = alloca i64, align 8
  %5 = alloca i64, align 8
  store ptr %0, ptr %3, align 8
  store i64 %1, ptr %4, align 8
  %6 = load ptr, ptr %3, align 8
  %7 = getelementptr inbounds %struct.CPU, ptr %6, i32 0, i32 0
  %8 = load i64, ptr %4, align 8
  %9 = lshr i64 %8, 2
  %10 = and i64 %9, 31
  %11 = getelementptr inbounds [32 x i64], ptr %7, i64 0, i64 %10
  %12 = load i64, ptr %11, align 8
  %13 = load i64, ptr %4, align 8
  %14 = add i64 %12, %13
  store i64 %14, ptr %5, align 8
  %15 = load i64, ptr %5, align 8
  %16 = mul i64 %15, 37
  %17 = load i64, ptr %5, align 8
  %18 = lshr i64 %17, 7
  %19 = xor i64 %16, %18
  store i64 %19, ptr %5, align 8
  %20 = load i64, ptr %5, align 8
  %21 = load ptr, ptr %3, align 8
  %22 = getelementptr inbounds %struct.CPU, ptr %21, i32 0, i32 0
  %23 = load i64, ptr %4, align 8
  %24 = lshr i64 %23, 2
  %25 = and i64 %24, 31
  %26 = getelementptr inbounds [32 x i64], ptr %22, i64 0, i64 %25
  store i64 %20, ptr %26, align 8
  %27 = load ptr, ptr %3, align 8
  %28 = load i64, ptr %4, align 8
  %29 = add i64 %28, 4
  %30 = call i64 @F96(ptr noundef %27, i64 noundef %29)
  %31 = load i64, ptr %5, align 8
  %32 = add i64 %30, %31
  ret i64 %32
}

; Function Attrs: nounwind
define dso_local i64 @F98(ptr noundef %0, i64 noundef %1) #0 {
  %3 = alloca ptr, align 8
  %4 = alloca i64, align 8
  %5 = alloca i64, align 8
  store ptr %0, ptr %3, align 8
  store i64 %1, ptr %4, align 8
  %6 = load ptr, ptr %3, align 8
  %7 = getelementptr inbounds %struct.CPU, ptr %6, i32 0, i32 0
  %8 = load i64, ptr %4, align 8
  %9 = lshr i64 %8, 2
  %10 = and i64 %9, 31
  %11 = getelementptr inbounds [32 x i64], ptr %7, i64 0, i64 %10
  %12 = load i64, ptr %11, align 8
  %13 = load i64, ptr %4, align 8
  %14 = add i64 %12, %13
  store i64 %14, ptr %5, align 8
  %15 = load i64, ptr %5, align 8
  %16 = mul i64 %15, 37
  %17 = load i64, ptr %5, align 8
  %18 = lshr i64 %17, 7
  %19 = xor i64 %16, %18
  store i64 %19, ptr %5, align 8
  %20 = load i64, ptr %5, align 8
  %21 = load ptr, ptr %3, align 8
  %22 = getelementptr inbounds %struct.CPU, ptr %21, i32 0, i32 0
  %23 = load i64, ptr %4, align 8
  %24 = lshr i64 %23, 2
  %25 = and i64 %24, 31
  %26 = getelementptr inbounds [32 x i64], ptr %22, i64 0, i64 %25
  store i64 %20, ptr %26, align 8
  %27 = load ptr, ptr %3, align 8
  %28 = load i64, ptr %4, align 8
  %29 = add i64 %28, 4
  %30 = call i64 @F97(ptr noundef %27, i64 noundef %29)
  %31 = load i64, ptr %5, align 8
  %32 = add i64 %30, %31
  ret i64 %32
}

; Function Attrs: nounwind
define dso_local i64 @F99(ptr noundef %0, i64 noundef %1) #0 {
  %3 = alloca ptr, align 8
  %4 = alloca i64, align 8
  %5 = alloca i64, align 8
  store ptr %0, ptr %3, align 8
  store i64 %1, ptr %4, align 8
  %6 = load ptr, ptr %3, align 8
  %7 = getelementptr inbounds %struct.CPU, ptr %6, i32 0, i32 0
  %8 = load i64, ptr %4, align 8
  %9 = lshr i64 %8, 2
  %10 = and i64 %9, 31
  %11 = getelementptr inbounds [32 x i64], ptr %7, i64 0, i64 %10
  %12 = load i64, ptr %11, align 8
  %13 = load i64, ptr %4, align 8
  %14 = add i64 %12, %13
  store i64 %14, ptr %5, align 8
  %15 = load i64, ptr %5, align 8
  %16 = mul i64 %15, 37
  %17 = load i64, ptr %5, align 8
  %18 = lshr i64 %17, 7
  %19 = xor i64 %16, %18
  store i64 %19, ptr %5, align 8
  %20 = load i64, ptr %5, align 8
  %21 = load ptr, ptr %3, align 8
  %22 = getelementptr inbounds %struct.CPU, ptr %21, i32 0, i32 0
  %23 = load i64, ptr %4, align 8
  %24 = lshr i64 %23, 2
  %25 = and i64 %24, 31
  %26 = getelementptr inbounds [32 x i64], ptr %22, i64 0, i64 %25
  store i64 %20, ptr %26, align 8
  %27 = load ptr, ptr %3, align 8
  %28 = load i64, ptr %4, align 8
  %29 = add i64 %28, 4
  %30 = call i64 @F98(ptr noundef %27, i64 noundef %29)
  %31 = load i64, ptr %5, align 8
  %32 = add i64 %30, %31
  ret i64 %32
}

; Function Attrs: nounwind
define dso_local i64 @F100(ptr noundef %0, i64 noundef %1) #0 {
  %3 = alloca ptr, align 8
  %4 = alloca i64, align 8
  %5 = alloca i64, align 8
  store ptr %0, ptr %3, align 8
  store i64 %1, ptr %4, align 8
  %6 = load ptr, ptr %3, align 8
  %7 = getelementptr inbounds %struct.CPU, ptr %6, i32 0, i32 0
  %8 = load i64, ptr %4, align 8
  %9 = lshr i64 %8, 2
  %10 = and i64 %9, 31
  %11 = getelementptr inbounds [32 x i64], ptr %7, i64 0, i64 %10
  %12 = load i64, ptr %11, align 8
  %13 = load i64, ptr %4, align 8
  %14 = add i64 %12, %13
  store i64 %14, ptr %5, align 8
  %15 = load i64, ptr %5, align 8
  %16 = mul i64 %15, 37
  %17 = load i64, ptr %5, align 8
  %18 = lshr i64 %17, 7
  %19 = xor i64 %16, %18
  store i64 %19, ptr %5, align 8
  %20 = load i64, ptr %5, align 8
  %21 = load ptr, ptr %3, align 8
  %22 = getelementptr inbounds %struct.CPU, ptr %21, i32 0, i32 0
  %23 = load i64, ptr %4, align 8
  %24 = lshr i64 %23, 2
  %25 = and i64 %24, 31
  %26 = getelementptr inbounds [32 x i64], ptr %22, i64 0, i64 %25
  store i64 %20, ptr %26, align 8
  %27 = load ptr, ptr %3, align 8
  %28 = load i64, ptr %4, align 8
  %29 = add i64 %28, 4
  %30 = call i64 @F99(ptr noundef %27, i64 noundef %29)
  %31 = load i64, ptr %5, align 8
  %32 = add i64 %30, %31
  ret i64 %32
}

; Function Attrs: nounwind
define dso_local i64 @F101(ptr noundef %0, i64 noundef %1) #0 {
  %3 = alloca ptr, align 8
  %4 = alloca i64, align 8
  %5 = alloca i64, align 8
  store ptr %0, ptr %3, align 8
  store i64 %1, ptr %4, align 8
  %6 = load ptr, ptr %3, align 8
  %7 = getelementptr inbounds %struct.CPU, ptr %6, i32 0, i32 0
  %8 = load i64, ptr %4, align 8
  %9 = lshr i64 %8, 2
  %10 = and i64 %9, 31
  %11 = getelementptr inbounds [32 x i64], ptr %7, i64 0, i64 %10
  %12 = load i64, ptr %11, align 8
  %13 = load i64, ptr %4, align 8
  %14 = add i64 %12, %13
  store i64 %14, ptr %5, align 8
  %15 = load i64, ptr %5, align 8
  %16 = mul i64 %15, 37
  %17 = load i64, ptr %5, align 8
  %18 = lshr i64 %17, 7
  %19 = xor i64 %16, %18
  store i64 %19, ptr %5, align 8
  %20 = load i64, ptr %5, align 8
  %21 = load ptr, ptr %3, align 8
  %22 = getelementptr inbounds %struct.CPU, ptr %21, i32 0, i32 0
  %23 = load i64, ptr %4, align 8
  %24 = lshr i64 %23, 2
  %25 = and i64 %24, 31
  %26 = getelementptr inbounds [32 x i64], ptr %22, i64 0, i64 %25
  store i64 %20, ptr %26, align 8
  %27 = load ptr, ptr %3, align 8
  %28 = load i64, ptr %4, align 8
  %29 = add i64 %28, 4
  %30 = call i64 @F100(ptr noundef %27, i64 noundef %29)
  %31 = load i64, ptr %5, align 8
  %32 = add i64 %30, %31
  ret i64 %32
}

; Function Attrs: nounwind
define dso_local i64 @F102(ptr noundef %0, i64 noundef %1) #0 {
  %3 = alloca ptr, align 8
  %4 = alloca i64, align 8
  %5 = alloca i64, align 8
  store ptr %0, ptr %3, align 8
  store i64 %1, ptr %4, align 8
  %6 = load ptr, ptr %3, align 8
  %7 = getelementptr inbounds %struct.CPU, ptr %6, i32 0, i32 0
  %8 = load i64, ptr %4, align 8
  %9 = lshr i64 %8, 2
  %10 = and i64 %9, 31
  %11 = getelementptr inbounds [32 x i64], ptr %7, i64 0, i64 %10
  %12 = load i64, ptr %11, align 8
  %13 = load i64, ptr %4, align 8
  %14 = add i64 %12, %13
  store i64 %14, ptr %5, align 8
  %15 = load i64, ptr %5, align 8
  %16 = mul i64 %15, 37
  %17 = load i64, ptr %5, align 8
  %18 = lshr i64 %17, 7
  %19 = xor i64 %16, %18
  store i64 %19, ptr %5, align 8
  %20 = load i64, ptr %5, align 8
  %21 = load ptr, ptr %3, align 8
  %22 = getelementptr inbounds %struct.CPU, ptr %21, i32 0, i32 0
  %23 = load i64, ptr %4, align 8
  %24 = lshr i64 %23, 2
  %25 = and i64 %24, 31
  %26 = getelementptr inbounds [32 x i64], ptr %22, i64 0, i64 %25
  store i64 %20, ptr %26, align 8
  %27 = load ptr, ptr %3, align 8
  %28 = load i64, ptr %4, align 8
  %29 = add i64 %28, 4
  %30 = call i64 @F101(ptr noundef %27, i64 noundef %29)
  %31 = load i64, ptr %5, align 8
  %32 = add i64 %30, %31
  ret i64 %32
}

; Function Attrs: nounwind
define dso_local i64 @F103(ptr noundef %0, i64 noundef %1) #0 {
  %3 = alloca ptr, align 8
  %4 = alloca i64, align 8
  %5 = alloca i64, align 8
  store ptr %0, ptr %3, align 8
  store i64 %1, ptr %4, align 8
  %6 = load ptr, ptr %3, align 8
  %7 = getelementptr inbounds %struct.CPU, ptr %6, i32 0, i32 0
  %8 = load i64, ptr %4, align 8
  %9 = lshr i64 %8, 2
  %10 = and i64 %9, 31
  %11 = getelementptr inbounds [32 x i64], ptr %7, i64 0, i64 %10
  %12 = load i64, ptr %11, align 8
  %13 = load i64, ptr %4, align 8
  %14 = add i64 %12, %13
  store i64 %14, ptr %5, align 8
  %15 = load i64, ptr %5, align 8
  %16 = mul i64 %15, 37
  %17 = load i64, ptr %5, align 8
  %18 = lshr i64 %17, 7
  %19 = xor i64 %16, %18
  store i64 %19, ptr %5, align 8
  %20 = load i64, ptr %5, align 8
  %21 = load ptr, ptr %3, align 8
  %22 = getelementptr inbounds %struct.CPU, ptr %21, i32 0, i32 0
  %23 = load i64, ptr %4, align 8
  %24 = lshr i64 %23, 2
  %25 = and i64 %24, 31
  %26 = getelementptr inbounds [32 x i64], ptr %22, i64 0, i64 %25
  store i64 %20, ptr %26, align 8
  %27 = load ptr, ptr %3, align 8
  %28 = load i64, ptr %4, align 8
  %29 = add i64 %28, 4
  %30 = call i64 @F102(ptr noundef %27, i64 noundef %29)
  %31 = load i64, ptr %5, align 8
  %32 = add i64 %30, %31
  ret i64 %32
}

; Function Attrs: nounwind
define dso_local i64 @F104(ptr noundef %0, i64 noundef %1) #0 {
  %3 = alloca ptr, align 8
  %4 = alloca i64, align 8
  %5 = alloca i64, align 8
  store ptr %0, ptr %3, align 8
  store i64 %1, ptr %4, align 8
  %6 = load ptr, ptr %3, align 8
  %7 = getelementptr inbounds %struct.CPU, ptr %6, i32 0, i32 0
  %8 = load i64, ptr %4, align 8
  %9 = lshr i64 %8, 2
  %10 = and i64 %9, 31
  %11 = getelementptr inbounds [32 x i64], ptr %7, i64 0, i64 %10
  %12 = load i64, ptr %11, align 8
  %13 = load i64, ptr %4, align 8
  %14 = add i64 %12, %13
  store i64 %14, ptr %5, align 8
  %15 = load i64, ptr %5, align 8
  %16 = mul i64 %15, 37
  %17 = load i64, ptr %5, align 8
  %18 = lshr i64 %17, 7
  %19 = xor i64 %16, %18
  store i64 %19, ptr %5, align 8
  %20 = load i64, ptr %5, align 8
  %21 = load ptr, ptr %3, align 8
  %22 = getelementptr inbounds %struct.CPU, ptr %21, i32 0, i32 0
  %23 = load i64, ptr %4, align 8
  %24 = lshr i64 %23, 2
  %25 = and i64 %24, 31
  %26 = getelementptr inbounds [32 x i64], ptr %22, i64 0, i64 %25
  store i64 %20, ptr %26, align 8
  %27 = load ptr, ptr %3, align 8
  %28 = load i64, ptr %4, align 8
  %29 = add i64 %28, 4
  %30 = call i64 @F103(ptr noundef %27, i64 noundef %29)
  %31 = load i64, ptr %5, align 8
  %32 = add i64 %30, %31
  ret i64 %32
}

; Function Attrs: nounwind
define dso_local i64 @F105(ptr noundef %0, i64 noundef %1) #0 {
  %3 = alloca ptr, align 8
  %4 = alloca i64, align 8
  %5 = alloca i64, align 8
  store ptr %0, ptr %3, align 8
  store i64 %1, ptr %4, align 8
  %6 = load ptr, ptr %3, align 8
  %7 = getelementptr inbounds %struct.CPU, ptr %6, i32 0, i32 0
  %8 = load i64, ptr %4, align 8
  %9 = lshr i64 %8, 2
  %10 = and i64 %9, 31
  %11 = getelementptr inbounds [32 x i64], ptr %7, i64 0, i64 %10
  %12 = load i64, ptr %11, align 8
  %13 = load i64, ptr %4, align 8
  %14 = add i64 %12, %13
  store i64 %14, ptr %5, align 8
  %15 = load i64, ptr %5, align 8
  %16 = mul i64 %15, 37
  %17 = load i64, ptr %5, align 8
  %18 = lshr i64 %17, 7
  %19 = xor i64 %16, %18
  store i64 %19, ptr %5, align 8
  %20 = load i64, ptr %5, align 8
  %21 = load ptr, ptr %3, align 8
  %22 = getelementptr inbounds %struct.CPU, ptr %21, i32 0, i32 0
  %23 = load i64, ptr %4, align 8
  %24 = lshr i64 %23, 2
  %25 = and i64 %24, 31
  %26 = getelementptr inbounds [32 x i64], ptr %22, i64 0, i64 %25
  store i64 %20, ptr %26, align 8
  %27 = load ptr, ptr %3, align 8
  %28 = load i64, ptr %4, align 8
  %29 = add i64 %28, 4
  %30 = call i64 @F104(ptr noundef %27, i64 noundef %29)
  %31 = load i64, ptr %5, align 8
  %32 = add i64 %30, %31
  ret i64 %32
}

; Function Attrs: nounwind
define dso_local i64 @F106(ptr noundef %0, i64 noundef %1) #0 {
  %3 = alloca ptr, align 8
  %4 = alloca i64, align 8
  %5 = alloca i64, align 8
  store ptr %0, ptr %3, align 8
  store i64 %1, ptr %4, align 8
  %6 = load ptr, ptr %3, align 8
  %7 = getelementptr inbounds %struct.CPU, ptr %6, i32 0, i32 0
  %8 = load i64, ptr %4, align 8
  %9 = lshr i64 %8, 2
  %10 = and i64 %9, 31
  %11 = getelementptr inbounds [32 x i64], ptr %7, i64 0, i64 %10
  %12 = load i64, ptr %11, align 8
  %13 = load i64, ptr %4, align 8
  %14 = add i64 %12, %13
  store i64 %14, ptr %5, align 8
  %15 = load i64, ptr %5, align 8
  %16 = mul i64 %15, 37
  %17 = load i64, ptr %5, align 8
  %18 = lshr i64 %17, 7
  %19 = xor i64 %16, %18
  store i64 %19, ptr %5, align 8
  %20 = load i64, ptr %5, align 8
  %21 = load ptr, ptr %3, align 8
  %22 = getelementptr inbounds %struct.CPU, ptr %21, i32 0, i32 0
  %23 = load i64, ptr %4, align 8
  %24 = lshr i64 %23, 2
  %25 = and i64 %24, 31
  %26 = getelementptr inbounds [32 x i64], ptr %22, i64 0, i64 %25
  store i64 %20, ptr %26, align 8
  %27 = load ptr, ptr %3, align 8
  %28 = load i64, ptr %4, align 8
  %29 = add i64 %28, 4
  %30 = call i64 @F105(ptr noundef %27, i64 noundef %29)
  %31 = load i64, ptr %5, align 8
  %32 = add i64 %30, %31
  ret i64 %32
}

; Function Attrs: nounwind
define dso_local i64 @F107(ptr noundef %0, i64 noundef %1) #0 {
  %3 = alloca ptr, align 8
  %4 = alloca i64, align 8
  %5 = alloca i64, align 8
  store ptr %0, ptr %3, align 8
  store i64 %1, ptr %4, align 8
  %6 = load ptr, ptr %3, align 8
  %7 = getelementptr inbounds %struct.CPU, ptr %6, i32 0, i32 0
  %8 = load i64, ptr %4, align 8
  %9 = lshr i64 %8, 2
  %10 = and i64 %9, 31
  %11 = getelementptr inbounds [32 x i64], ptr %7, i64 0, i64 %10
  %12 = load i64, ptr %11, align 8
  %13 = load i64, ptr %4, align 8
  %14 = add i64 %12, %13
  store i64 %14, ptr %5, align 8
  %15 = load i64, ptr %5, align 8
  %16 = mul i64 %15, 37
  %17 = load i64, ptr %5, align 8
  %18 = lshr i64 %17, 7
  %19 = xor i64 %16, %18
  store i64 %19, ptr %5, align 8
  %20 = load i64, ptr %5, align 8
  %21 = load ptr, ptr %3, align 8
  %22 = getelementptr inbounds %struct.CPU, ptr %21, i32 0, i32 0
  %23 = load i64, ptr %4, align 8
  %24 = lshr i64 %23, 2
  %25 = and i64 %24, 31
  %26 = getelementptr inbounds [32 x i64], ptr %22, i64 0, i64 %25
  store i64 %20, ptr %26, align 8
  %27 = load ptr, ptr %3, align 8
  %28 = load i64, ptr %4, align 8
  %29 = add i64 %28, 4
  %30 = call i64 @F106(ptr noundef %27, i64 noundef %29)
  %31 = load i64, ptr %5, align 8
  %32 = add i64 %30, %31
  ret i64 %32
}

; Function Attrs: nounwind
define dso_local i64 @F108(ptr noundef %0, i64 noundef %1) #0 {
  %3 = alloca ptr, align 8
  %4 = alloca i64, align 8
  %5 = alloca i64, align 8
  store ptr %0, ptr %3, align 8
  store i64 %1, ptr %4, align 8
  %6 = load ptr, ptr %3, align 8
  %7 = getelementptr inbounds %struct.CPU, ptr %6, i32 0, i32 0
  %8 = load i64, ptr %4, align 8
  %9 = lshr i64 %8, 2
  %10 = and i64 %9, 31
  %11 = getelementptr inbounds [32 x i64], ptr %7, i64 0, i64 %10
  %12 = load i64, ptr %11, align 8
  %13 = load i64, ptr %4, align 8
  %14 = add i64 %12, %13
  store i64 %14, ptr %5, align 8
  %15 = load i64, ptr %5, align 8
  %16 = mul i64 %15, 37
  %17 = load i64, ptr %5, align 8
  %18 = lshr i64 %17, 7
  %19 = xor i64 %16, %18
  store i64 %19, ptr %5, align 8
  %20 = load i64, ptr %5, align 8
  %21 = load ptr, ptr %3, align 8
  %22 = getelementptr inbounds %struct.CPU, ptr %21, i32 0, i32 0
  %23 = load i64, ptr %4, align 8
  %24 = lshr i64 %23, 2
  %25 = and i64 %24, 31
  %26 = getelementptr inbounds [32 x i64], ptr %22, i64 0, i64 %25
  store i64 %20, ptr %26, align 8
  %27 = load ptr, ptr %3, align 8
  %28 = load i64, ptr %4, align 8
  %29 = add i64 %28, 4
  %30 = call i64 @F107(ptr noundef %27, i64 noundef %29)
  %31 = load i64, ptr %5, align 8
  %32 = add i64 %30, %31
  ret i64 %32
}

; Function Attrs: nounwind
define dso_local i64 @F109(ptr noundef %0, i64 noundef %1) #0 {
  %3 = alloca ptr, align 8
  %4 = alloca i64, align 8
  %5 = alloca i64, align 8
  store ptr %0, ptr %3, align 8
  store i64 %1, ptr %4, align 8
  %6 = load ptr, ptr %3, align 8
  %7 = getelementptr inbounds %struct.CPU, ptr %6, i32 0, i32 0
  %8 = load i64, ptr %4, align 8
  %9 = lshr i64 %8, 2
  %10 = and i64 %9, 31
  %11 = getelementptr inbounds [32 x i64], ptr %7, i64 0, i64 %10
  %12 = load i64, ptr %11, align 8
  %13 = load i64, ptr %4, align 8
  %14 = add i64 %12, %13
  store i64 %14, ptr %5, align 8
  %15 = load i64, ptr %5, align 8
  %16 = mul i64 %15, 37
  %17 = load i64, ptr %5, align 8
  %18 = lshr i64 %17, 7
  %19 = xor i64 %16, %18
  store i64 %19, ptr %5, align 8
  %20 = load i64, ptr %5, align 8
  %21 = load ptr, ptr %3, align 8
  %22 = getelementptr inbounds %struct.CPU, ptr %21, i32 0, i32 0
  %23 = load i64, ptr %4, align 8
  %24 = lshr i64 %23, 2
  %25 = and i64 %24, 31
  %26 = getelementptr inbounds [32 x i64], ptr %22, i64 0, i64 %25
  store i64 %20, ptr %26, align 8
  %27 = load ptr, ptr %3, align 8
  %28 = load i64, ptr %4, align 8
  %29 = add i64 %28, 4
  %30 = call i64 @F108(ptr noundef %27, i64 noundef %29)
  %31 = load i64, ptr %5, align 8
  %32 = add i64 %30, %31
  ret i64 %32
}

; Function Attrs: nounwind
define dso_local i64 @F110(ptr noundef %0, i64 noundef %1) #0 {
  %3 = alloca ptr, align 8
  %4 = alloca i64, align 8
  %5 = alloca i64, align 8
  store ptr %0, ptr %3, align 8
  store i64 %1, ptr %4, align 8
  %6 = load ptr, ptr %3, align 8
  %7 = getelementptr inbounds %struct.CPU, ptr %6, i32 0, i32 0
  %8 = load i64, ptr %4, align 8
  %9 = lshr i64 %8, 2
  %10 = and i64 %9, 31
  %11 = getelementptr inbounds [32 x i64], ptr %7, i64 0, i64 %10
  %12 = load i64, ptr %11, align 8
  %13 = load i64, ptr %4, align 8
  %14 = add i64 %12, %13
  store i64 %14, ptr %5, align 8
  %15 = load i64, ptr %5, align 8
  %16 = mul i64 %15, 37
  %17 = load i64, ptr %5, align 8
  %18 = lshr i64 %17, 7
  %19 = xor i64 %16, %18
  store i64 %19, ptr %5, align 8
  %20 = load i64, ptr %5, align 8
  %21 = load ptr, ptr %3, align 8
  %22 = getelementptr inbounds %struct.CPU, ptr %21, i32 0, i32 0
  %23 = load i64, ptr %4, align 8
  %24 = lshr i64 %23, 2
  %25 = and i64 %24, 31
  %26 = getelementptr inbounds [32 x i64], ptr %22, i64 0, i64 %25
  store i64 %20, ptr %26, align 8
  %27 = load ptr, ptr %3, align 8
  %28 = load i64, ptr %4, align 8
  %29 = add i64 %28, 4
  %30 = call i64 @F109(ptr noundef %27, i64 noundef %29)
  %31 = load i64, ptr %5, align 8
  %32 = add i64 %30, %31
  ret i64 %32
}

; Function Attrs: nounwind
define dso_local i64 @F111(ptr noundef %0, i64 noundef %1) #0 {
  %3 = alloca ptr, align 8
  %4 = alloca i64, align 8
  %5 = alloca i64, align 8
  store ptr %0, ptr %3, align 8
  store i64 %1, ptr %4, align 8
  %6 = load ptr, ptr %3, align 8
  %7 = getelementptr inbounds %struct.CPU, ptr %6, i32 0, i32 0
  %8 = load i64, ptr %4, align 8
  %9 = lshr i64 %8, 2
  %10 = and i64 %9, 31
  %11 = getelementptr inbounds [32 x i64], ptr %7, i64 0, i64 %10
  %12 = load i64, ptr %11, align 8
  %13 = load i64, ptr %4, align 8
  %14 = add i64 %12, %13
  store i64 %14, ptr %5, align 8
  %15 = load i64, ptr %5, align 8
  %16 = mul i64 %15, 37
  %17 = load i64, ptr %5, align 8
  %18 = lshr i64 %17, 7
  %19 = xor i64 %16, %18
  store i64 %19, ptr %5, align 8
  %20 = load i64, ptr %5, align 8
  %21 = load ptr, ptr %3, align 8
  %22 = getelementptr inbounds %struct.CPU, ptr %21, i32 0, i32 0
  %23 = load i64, ptr %4, align 8
  %24 = lshr i64 %23, 2
  %25 = and i64 %24, 31
  %26 = getelementptr inbounds [32 x i64], ptr %22, i64 0, i64 %25
  store i64 %20, ptr %26, align 8
  %27 = load ptr, ptr %3, align 8
  %28 = load i64, ptr %4, align 8
  %29 = add i64 %28, 4
  %30 = call i64 @F110(ptr noundef %27, i64 noundef %29)
  %31 = load i64, ptr %5, align 8
  %32 = add i64 %30, %31
  ret i64 %32
}

; Function Attrs: nounwind
define dso_local i64 @F112(ptr noundef %0, i64 noundef %1) #0 {
  %3 = alloca ptr, align 8
  %4 = alloca i64, align 8
  %5 = alloca i64, align 8
  store ptr %0, ptr %3, align 8
  store i64 %1, ptr %4, align 8
  %6 = load ptr, ptr %3, align 8
  %7 = getelementptr inbounds %struct.CPU, ptr %6, i32 0, i32 0
  %8 = load i64, ptr %4, align 8
  %9 = lshr i64 %8, 2
  %10 = and i64 %9, 31
  %11 = getelementptr inbounds [32 x i64], ptr %7, i64 0, i64 %10
  %12 = load i64, ptr %11, align 8
  %13 = load i64, ptr %4, align 8
  %14 = add i64 %12, %13
  store i64 %14, ptr %5, align 8
  %15 = load i64, ptr %5, align 8
  %16 = mul i64 %15, 37
  %17 = load i64, ptr %5, align 8
  %18 = lshr i64 %17, 7
  %19 = xor i64 %16, %18
  store i64 %19, ptr %5, align 8
  %20 = load i64, ptr %5, align 8
  %21 = load ptr, ptr %3, align 8
  %22 = getelementptr inbounds %struct.CPU, ptr %21, i32 0, i32 0
  %23 = load i64, ptr %4, align 8
  %24 = lshr i64 %23, 2
  %25 = and i64 %24, 31
  %26 = getelementptr inbounds [32 x i64], ptr %22, i64 0, i64 %25
  store i64 %20, ptr %26, align 8
  %27 = load ptr, ptr %3, align 8
  %28 = load i64, ptr %4, align 8
  %29 = add i64 %28, 4
  %30 = call i64 @F111(ptr noundef %27, i64 noundef %29)
  %31 = load i64, ptr %5, align 8
  %32 = add i64 %30, %31
  ret i64 %32
}

; Function Attrs: nounwind
define dso_local i64 @F113(ptr noundef %0, i64 noundef %1) #0 {
  %3 = alloca ptr, align 8
  %4 = alloca i64, align 8
  %5 = alloca i64, align 8
  store ptr %0, ptr %3, align 8
  store i64 %1, ptr %4, align 8
  %6 = load ptr, ptr %3, align 8
  %7 = getelementptr inbounds %struct.CPU, ptr %6, i32 0, i32 0
  %8 = load i64, ptr %4, align 8
  %9 = lshr i64 %8, 2
  %10 = and i64 %9, 31
  %11 = getelementptr inbounds [32 x i64], ptr %7, i64 0, i64 %10
  %12 = load i64, ptr %11, align 8
  %13 = load i64, ptr %4, align 8
  %14 = add i64 %12, %13
  store i64 %14, ptr %5, align 8
  %15 = load i64, ptr %5, align 8
  %16 = mul i64 %15, 37
  %17 = load i64, ptr %5, align 8
  %18 = lshr i64 %17, 7
  %19 = xor i64 %16, %18
  store i64 %19, ptr %5, align 8
  %20 = load i64, ptr %5, align 8
  %21 = load ptr, ptr %3, align 8
  %22 = getelementptr inbounds %struct.CPU, ptr %21, i32 0, i32 0
  %23 = load i64, ptr %4, align 8
  %24 = lshr i64 %23, 2
  %25 = and i64 %24, 31
  %26 = getelementptr inbounds [32 x i64], ptr %22, i64 0, i64 %25
  store i64 %20, ptr %26, align 8
  %27 = load ptr, ptr %3, align 8
  %28 = load i64, ptr %4, align 8
  %29 = add i64 %28, 4
  %30 = call i64 @F112(ptr noundef %27, i64 noundef %29)
  %31 = load i64, ptr %5, align 8
  %32 = add i64 %30, %31
  ret i64 %32
}

; Function Attrs: nounwind
define dso_local i64 @F114(ptr noundef %0, i64 noundef %1) #0 {
  %3 = alloca ptr, align 8
  %4 = alloca i64, align 8
  %5 = alloca i64, align 8
  store ptr %0, ptr %3, align 8
  store i64 %1, ptr %4, align 8
  %6 = load ptr, ptr %3, align 8
  %7 = getelementptr inbounds %struct.CPU, ptr %6, i32 0, i32 0
  %8 = load i64, ptr %4, align 8
  %9 = lshr i64 %8, 2
  %10 = and i64 %9, 31
  %11 = getelementptr inbounds [32 x i64], ptr %7, i64 0, i64 %10
  %12 = load i64, ptr %11, align 8
  %13 = load i64, ptr %4, align 8
  %14 = add i64 %12, %13
  store i64 %14, ptr %5, align 8
  %15 = load i64, ptr %5, align 8
  %16 = mul i64 %15, 37
  %17 = load i64, ptr %5, align 8
  %18 = lshr i64 %17, 7
  %19 = xor i64 %16, %18
  store i64 %19, ptr %5, align 8
  %20 = load i64, ptr %5, align 8
  %21 = load ptr, ptr %3, align 8
  %22 = getelementptr inbounds %struct.CPU, ptr %21, i32 0, i32 0
  %23 = load i64, ptr %4, align 8
  %24 = lshr i64 %23, 2
  %25 = and i64 %24, 31
  %26 = getelementptr inbounds [32 x i64], ptr %22, i64 0, i64 %25
  store i64 %20, ptr %26, align 8
  %27 = load ptr, ptr %3, align 8
  %28 = load i64, ptr %4, align 8
  %29 = add i64 %28, 4
  %30 = call i64 @F113(ptr noundef %27, i64 noundef %29)
  %31 = load i64, ptr %5, align 8
  %32 = add i64 %30, %31
  ret i64 %32
}

; Function Attrs: nounwind
define dso_local i64 @F115(ptr noundef %0, i64 noundef %1) #0 {
  %3 = alloca ptr, align 8
  %4 = alloca i64, align 8
  %5 = alloca i64, align 8
  store ptr %0, ptr %3, align 8
  store i64 %1, ptr %4, align 8
  %6 = load ptr, ptr %3, align 8
  %7 = getelementptr inbounds %struct.CPU, ptr %6, i32 0, i32 0
  %8 = load i64, ptr %4, align 8
  %9 = lshr i64 %8, 2
  %10 = and i64 %9, 31
  %11 = getelementptr inbounds [32 x i64], ptr %7, i64 0, i64 %10
  %12 = load i64, ptr %11, align 8
  %13 = load i64, ptr %4, align 8
  %14 = add i64 %12, %13
  store i64 %14, ptr %5, align 8
  %15 = load i64, ptr %5, align 8
  %16 = mul i64 %15, 37
  %17 = load i64, ptr %5, align 8
  %18 = lshr i64 %17, 7
  %19 = xor i64 %16, %18
  store i64 %19, ptr %5, align 8
  %20 = load i64, ptr %5, align 8
  %21 = load ptr, ptr %3, align 8
  %22 = getelementptr inbounds %struct.CPU, ptr %21, i32 0, i32 0
  %23 = load i64, ptr %4, align 8
  %24 = lshr i64 %23, 2
  %25 = and i64 %24, 31
  %26 = getelementptr inbounds [32 x i64], ptr %22, i64 0, i64 %25
  store i64 %20, ptr %26, align 8
  %27 = load ptr, ptr %3, align 8
  %28 = load i64, ptr %4, align 8
  %29 = add i64 %28, 4
  %30 = call i64 @F114(ptr noundef %27, i64 noundef %29)
  %31 = load i64, ptr %5, align 8
  %32 = add i64 %30, %31
  ret i64 %32
}

; Function Attrs: nounwind
define dso_local i64 @F116(ptr noundef %0, i64 noundef %1) #0 {
  %3 = alloca ptr, align 8
  %4 = alloca i64, align 8
  %5 = alloca i64, align 8
  store ptr %0, ptr %3, align 8
  store i64 %1, ptr %4, align 8
  %6 = load ptr, ptr %3, align 8
  %7 = getelementptr inbounds %struct.CPU, ptr %6, i32 0, i32 0
  %8 = load i64, ptr %4, align 8
  %9 = lshr i64 %8, 2
  %10 = and i64 %9, 31
  %11 = getelementptr inbounds [32 x i64], ptr %7, i64 0, i64 %10
  %12 = load i64, ptr %11, align 8
  %13 = load i64, ptr %4, align 8
  %14 = add i64 %12, %13
  store i64 %14, ptr %5, align 8
  %15 = load i64, ptr %5, align 8
  %16 = mul i64 %15, 37
  %17 = load i64, ptr %5, align 8
  %18 = lshr i64 %17, 7
  %19 = xor i64 %16, %18
  store i64 %19, ptr %5, align 8
  %20 = load i64, ptr %5, align 8
  %21 = load ptr, ptr %3, align 8
  %22 = getelementptr inbounds %struct.CPU, ptr %21, i32 0, i32 0
  %23 = load i64, ptr %4, align 8
  %24 = lshr i64 %23, 2
  %25 = and i64 %24, 31
  %26 = getelementptr inbounds [32 x i64], ptr %22, i64 0, i64 %25
  store i64 %20, ptr %26, align 8
  %27 = load ptr, ptr %3, align 8
  %28 = load i64, ptr %4, align 8
  %29 = add i64 %28, 4
  %30 = call i64 @F115(ptr noundef %27, i64 noundef %29)
  %31 = load i64, ptr %5, align 8
  %32 = add i64 %30, %31
  ret i64 %32
}

; Function Attrs: nounwind
define dso_local i64 @F117(ptr noundef %0, i64 noundef %1) #0 {
  %3 = alloca ptr, align 8
  %4 = alloca i64, align 8
  %5 = alloca i64, align 8
  store ptr %0, ptr %3, align 8
  store i64 %1, ptr %4, align 8
  %6 = load ptr, ptr %3, align 8
  %7 = getelementptr inbounds %struct.CPU, ptr %6, i32 0, i32 0
  %8 = load i64, ptr %4, align 8
  %9 = lshr i64 %8, 2
  %10 = and i64 %9, 31
  %11 = getelementptr inbounds [32 x i64], ptr %7, i64 0, i64 %10
  %12 = load i64, ptr %11, align 8
  %13 = load i64, ptr %4, align 8
  %14 = add i64 %12, %13
  store i64 %14, ptr %5, align 8
  %15 = load i64, ptr %5, align 8
  %16 = mul i64 %15, 37
  %17 = load i64, ptr %5, align 8
  %18 = lshr i64 %17, 7
  %19 = xor i64 %16, %18
  store i64 %19, ptr %5, align 8
  %20 = load i64, ptr %5, align 8
  %21 = load ptr, ptr %3, align 8
  %22 = getelementptr inbounds %struct.CPU, ptr %21, i32 0, i32 0
  %23 = load i64, ptr %4, align 8
  %24 = lshr i64 %23, 2
  %25 = and i64 %24, 31
  %26 = getelementptr inbounds [32 x i64], ptr %22, i64 0, i64 %25
  store i64 %20, ptr %26, align 8
  %27 = load ptr, ptr %3, align 8
  %28 = load i64, ptr %4, align 8
  %29 = add i64 %28, 4
  %30 = call i64 @F116(ptr noundef %27, i64 noundef %29)
  %31 = load i64, ptr %5, align 8
  %32 = add i64 %30, %31
  ret i64 %32
}

; Function Attrs: nounwind
define dso_local i64 @F118(ptr noundef %0, i64 noundef %1) #0 {
  %3 = alloca ptr, align 8
  %4 = alloca i64, align 8
  %5 = alloca i64, align 8
  store ptr %0, ptr %3, align 8
  store i64 %1, ptr %4, align 8
  %6 = load ptr, ptr %3, align 8
  %7 = getelementptr inbounds %struct.CPU, ptr %6, i32 0, i32 0
  %8 = load i64, ptr %4, align 8
  %9 = lshr i64 %8, 2
  %10 = and i64 %9, 31
  %11 = getelementptr inbounds [32 x i64], ptr %7, i64 0, i64 %10
  %12 = load i64, ptr %11, align 8
  %13 = load i64, ptr %4, align 8
  %14 = add i64 %12, %13
  store i64 %14, ptr %5, align 8
  %15 = load i64, ptr %5, align 8
  %16 = mul i64 %15, 37
  %17 = load i64, ptr %5, align 8
  %18 = lshr i64 %17, 7
  %19 = xor i64 %16, %18
  store i64 %19, ptr %5, align 8
  %20 = load i64, ptr %5, align 8
  %21 = load ptr, ptr %3, align 8
  %22 = getelementptr inbounds %struct.CPU, ptr %21, i32 0, i32 0
  %23 = load i64, ptr %4, align 8
  %24 = lshr i64 %23, 2
  %25 = and i64 %24, 31
  %26 = getelementptr inbounds [32 x i64], ptr %22, i64 0, i64 %25
  store i64 %20, ptr %26, align 8
  %27 = load ptr, ptr %3, align 8
  %28 = load i64, ptr %4, align 8
  %29 = add i64 %28, 4
  %30 = call i64 @F117(ptr noundef %27, i64 noundef %29)
  %31 = load i64, ptr %5, align 8
  %32 = add i64 %30, %31
  ret i64 %32
}

; Function Attrs: nounwind
define dso_local i64 @F119(ptr noundef %0, i64 noundef %1) #0 {
  %3 = alloca ptr, align 8
  %4 = alloca i64, align 8
  %5 = alloca i64, align 8
  store ptr %0, ptr %3, align 8
  store i64 %1, ptr %4, align 8
  %6 = load ptr, ptr %3, align 8
  %7 = getelementptr inbounds %struct.CPU, ptr %6, i32 0, i32 0
  %8 = load i64, ptr %4, align 8
  %9 = lshr i64 %8, 2
  %10 = and i64 %9, 31
  %11 = getelementptr inbounds [32 x i64], ptr %7, i64 0, i64 %10
  %12 = load i64, ptr %11, align 8
  %13 = load i64, ptr %4, align 8
  %14 = add i64 %12, %13
  store i64 %14, ptr %5, align 8
  %15 = load i64, ptr %5, align 8
  %16 = mul i64 %15, 37
  %17 = load i64, ptr %5, align 8
  %18 = lshr i64 %17, 7
  %19 = xor i64 %16, %18
  store i64 %19, ptr %5, align 8
  %20 = load i64, ptr %5, align 8
  %21 = load ptr, ptr %3, align 8
  %22 = getelementptr inbounds %struct.CPU, ptr %21, i32 0, i32 0
  %23 = load i64, ptr %4, align 8
  %24 = lshr i64 %23, 2
  %25 = and i64 %24, 31
  %26 = getelementptr inbounds [32 x i64], ptr %22, i64 0, i64 %25
  store i64 %20, ptr %26, align 8
  %27 = load ptr, ptr %3, align 8
  %28 = load i64, ptr %4, align 8
  %29 = add i64 %28, 4
  %30 = call i64 @F118(ptr noundef %27, i64 noundef %29)
  %31 = load i64, ptr %5, align 8
  %32 = add i64 %30, %31
  ret i64 %32
}

; Function Attrs: nounwind
define dso_local i64 @F120(ptr noundef %0, i64 noundef %1) #0 {
  %3 = alloca ptr, align 8
  %4 = alloca i64, align 8
  %5 = alloca i64, align 8
  store ptr %0, ptr %3, align 8
  store i64 %1, ptr %4, align 8
  %6 = load ptr, ptr %3, align 8
  %7 = getelementptr inbounds %struct.CPU, ptr %6, i32 0, i32 0
  %8 = load i64, ptr %4, align 8
  %9 = lshr i64 %8, 2
  %10 = and i64 %9, 31
  %11 = getelementptr inbounds [32 x i64], ptr %7, i64 0, i64 %10
  %12 = load i64, ptr %11, align 8
  %13 = load i64, ptr %4, align 8
  %14 = add i64 %12, %13
  store i64 %14, ptr %5, align 8
  %15 = load i64, ptr %5, align 8
  %16 = mul i64 %15, 37
  %17 = load i64, ptr %5, align 8
  %18 = lshr i64 %17, 7
  %19 = xor i64 %16, %18
  store i64 %19, ptr %5, align 8
  %20 = load i64, ptr %5, align 8
  %21 = load ptr, ptr %3, align 8
  %22 = getelementptr inbounds %struct.CPU, ptr %21, i32 0, i32 0
  %23 = load i64, ptr %4, align 8
  %24 = lshr i64 %23, 2
  %25 = and i64 %24, 31
  %26 = getelementptr inbounds [32 x i64], ptr %22, i64 0, i64 %25
  store i64 %20, ptr %26, align 8
  %27 = load ptr, ptr %3, align 8
  %28 = load i64, ptr %4, align 8
  %29 = add i64 %28, 4
  %30 = call i64 @F119(ptr noundef %27, i64 noundef %29)
  %31 = load i64, ptr %5, align 8
  %32 = add i64 %30, %31
  ret i64 %32
}

; Function Attrs: nounwind
define dso_local i64 @F121(ptr noundef %0, i64 noundef %1) #0 {
  %3 = alloca ptr, align 8
  %4 = alloca i64, align 8
  %5 = alloca i64, align 8
  store ptr %0, ptr %3, align 8
  store i64 %1, ptr %4, align 8
  %6 = load ptr, ptr %3, align 8
  %7 = getelementptr inbounds %struct.CPU, ptr %6, i32 0, i32 0
  %8 = load i64, ptr %4, align 8
  %9 = lshr i64 %8, 2
  %10 = and i64 %9, 31
  %11 = getelementptr inbounds [32 x i64], ptr %7, i64 0, i64 %10
  %12 = load i64, ptr %11, align 8
  %13 = load i64, ptr %4, align 8
  %14 = add i64 %12, %13
  store i64 %14, ptr %5, align 8
  %15 = load i64, ptr %5, align 8
  %16 = mul i64 %15, 37
  %17 = load i64, ptr %5, align 8
  %18 = lshr i64 %17, 7
  %19 = xor i64 %16, %18
  store i64 %19, ptr %5, align 8
  %20 = load i64, ptr %5, align 8
  %21 = load ptr, ptr %3, align 8
  %22 = getelementptr inbounds %struct.CPU, ptr %21, i32 0, i32 0
  %23 = load i64, ptr %4, align 8
  %24 = lshr i64 %23, 2
  %25 = and i64 %24, 31
  %26 = getelementptr inbounds [32 x i64], ptr %22, i64 0, i64 %25
  store i64 %20, ptr %26, align 8
  %27 = load ptr, ptr %3, align 8
  %28 = load i64, ptr %4, align 8
  %29 = add i64 %28, 4
  %30 = call i64 @F120(ptr noundef %27, i64 noundef %29)
  %31 = load i64, ptr %5, align 8
  %32 = add i64 %30, %31
  ret i64 %32
}

; Function Attrs: nounwind
define dso_local i64 @F122(ptr noundef %0, i64 noundef %1) #0 {
  %3 = alloca ptr, align 8
  %4 = alloca i64, align 8
  %5 = alloca i64, align 8
  store ptr %0, ptr %3, align 8
  store i64 %1, ptr %4, align 8
  %6 = load ptr, ptr %3, align 8
  %7 = getelementptr inbounds %struct.CPU, ptr %6, i32 0, i32 0
  %8 = load i64, ptr %4, align 8
  %9 = lshr i64 %8, 2
  %10 = and i64 %9, 31
  %11 = getelementptr inbounds [32 x i64], ptr %7, i64 0, i64 %10
  %12 = load i64, ptr %11, align 8
  %13 = load i64, ptr %4, align 8
  %14 = add i64 %12, %13
  store i64 %14, ptr %5, align 8
  %15 = load i64, ptr %5, align 8
  %16 = mul i64 %15, 37
  %17 = load i64, ptr %5, align 8
  %18 = lshr i64 %17, 7
  %19 = xor i64 %16, %18
  store i64 %19, ptr %5, align 8
  %20 = load i64, ptr %5, align 8
  %21 = load ptr, ptr %3, align 8
  %22 = getelementptr inbounds %struct.CPU, ptr %21, i32 0, i32 0
  %23 = load i64, ptr %4, align 8
  %24 = lshr i64 %23, 2
  %25 = and i64 %24, 31
  %26 = getelementptr inbounds [32 x i64], ptr %22, i64 0, i64 %25
  store i64 %20, ptr %26, align 8
  %27 = load ptr, ptr %3, align 8
  %28 = load i64, ptr %4, align 8
  %29 = add i64 %28, 4
  %30 = call i64 @F121(ptr noundef %27, i64 noundef %29)
  %31 = load i64, ptr %5, align 8
  %32 = add i64 %30, %31
  ret i64 %32
}

; Function Attrs: nounwind
define dso_local i64 @F123(ptr noundef %0, i64 noundef %1) #0 {
  %3 = alloca ptr, align 8
  %4 = alloca i64, align 8
  %5 = alloca i64, align 8
  store ptr %0, ptr %3, align 8
  store i64 %1, ptr %4, align 8
  %6 = load ptr, ptr %3, align 8
  %7 = getelementptr inbounds %struct.CPU, ptr %6, i32 0, i32 0
  %8 = load i64, ptr %4, align 8
  %9 = lshr i64 %8, 2
  %10 = and i64 %9, 31
  %11 = getelementptr inbounds [32 x i64], ptr %7, i64 0, i64 %10
  %12 = load i64, ptr %11, align 8
  %13 = load i64, ptr %4, align 8
  %14 = add i64 %12, %13
  store i64 %14, ptr %5, align 8
  %15 = load i64, ptr %5, align 8
  %16 = mul i64 %15, 37
  %17 = load i64, ptr %5, align 8
  %18 = lshr i64 %17, 7
  %19 = xor i64 %16, %18
  store i64 %19, ptr %5, align 8
  %20 = load i64, ptr %5, align 8
  %21 = load ptr, ptr %3, align 8
  %22 = getelementptr inbounds %struct.CPU, ptr %21, i32 0, i32 0
  %23 = load i64, ptr %4, align 8
  %24 = lshr i64 %23, 2
  %25 = and i64 %24, 31
  %26 = getelementptr inbounds [32 x i64], ptr %22, i64 0, i64 %25
  store i64 %20, ptr %26, align 8
  %27 = load ptr, ptr %3, align 8
  %28 = load i64, ptr %4, align 8
  %29 = add i64 %28, 4
  %30 = call i64 @F122(ptr noundef %27, i64 noundef %29)
  %31 = load i64, ptr %5, align 8
  %32 = add i64 %30, %31
  ret i64 %32
}

; Function Attrs: nounwind
define dso_local i64 @F124(ptr noundef %0, i64 noundef %1) #0 {
  %3 = alloca ptr, align 8
  %4 = alloca i64, align 8
  %5 = alloca i64, align 8
  store ptr %0, ptr %3, align 8
  store i64 %1, ptr %4, align 8
  %6 = load ptr, ptr %3, align 8
  %7 = getelementptr inbounds %struct.CPU, ptr %6, i32 0, i32 0
  %8 = load i64, ptr %4, align 8
  %9 = lshr i64 %8, 2
  %10 = and i64 %9, 31
  %11 = getelementptr inbounds [32 x i64], ptr %7, i64 0, i64 %10
  %12 = load i64, ptr %11, align 8
  %13 = load i64, ptr %4, align 8
  %14 = add i64 %12, %13
  store i64 %14, ptr %5, align 8
  %15 = load i64, ptr %5, align 8
  %16 = mul i64 %15, 37
  %17 = load i64, ptr %5, align 8
  %18 = lshr i64 %17, 7
  %19 = xor i64 %16, %18
  store i64 %19, ptr %5, align 8
  %20 = load i64, ptr %5, align 8
  %21 = load ptr, ptr %3, align 8
  %22 = getelementptr inbounds %struct.CPU, ptr %21, i32 0, i32 0
  %23 = load i64, ptr %4, align 8
  %24 = lshr i64 %23, 2
  %25 = and i64 %24, 31
  %26 = getelementptr inbounds [32 x i64], ptr %22, i64 0, i64 %25
  store i64 %20, ptr %26, align 8
  %27 = load ptr, ptr %3, align 8
  %28 = load i64, ptr %4, align 8
  %29 = add i64 %28, 4
  %30 = call i64 @F123(ptr noundef %27, i64 noundef %29)
  %31 = load i64, ptr %5, align 8
  %32 = add i64 %30, %31
  ret i64 %32
}

; Function Attrs: nounwind
define dso_local i64 @F125(ptr noundef %0, i64 noundef %1) #0 {
  %3 = alloca ptr, align 8
  %4 = alloca i64, align 8
  %5 = alloca i64, align 8
  store ptr %0, ptr %3, align 8
  store i64 %1, ptr %4, align 8
  %6 = load ptr, ptr %3, align 8
  %7 = getelementptr inbounds %struct.CPU, ptr %6, i32 0, i32 0
  %8 = load i64, ptr %4, align 8
  %9 = lshr i64 %8, 2
  %10 = and i64 %9, 31
  %11 = getelementptr inbounds [32 x i64], ptr %7, i64 0, i64 %10
  %12 = load i64, ptr %11, align 8
  %13 = load i64, ptr %4, align 8
  %14 = add i64 %12, %13
  store i64 %14, ptr %5, align 8
  %15 = load i64, ptr %5, align 8
  %16 = mul i64 %15, 37
  %17 = load i64, ptr %5, align 8
  %18 = lshr i64 %17, 7
  %19 = xor i64 %16, %18
  store i64 %19, ptr %5, align 8
  %20 = load i64, ptr %5, align 8
  %21 = load ptr, ptr %3, align 8
  %22 = getelementptr inbounds %struct.CPU, ptr %21, i32 0, i32 0
  %23 = load i64, ptr %4, align 8
  %24 = lshr i64 %23, 2
  %25 = and i64 %24, 31
  %26 = getelementptr inbounds [32 x i64], ptr %22, i64 0, i64 %25
  store i64 %20, ptr %26, align 8
  %27 = load ptr, ptr %3, align 8
  %28 = load i64, ptr %4, align 8
  %29 = add i64 %28, 4
  %30 = call i64 @F124(ptr noundef %27, i64 noundef %29)
  %31 = load i64, ptr %5, align 8
  %32 = add i64 %30, %31
  ret i64 %32
}

; Function Attrs: nounwind
define dso_local i64 @F126(ptr noundef %0, i64 noundef %1) #0 {
  %3 = alloca ptr, align 8
  %4 = alloca i64, align 8
  %5 = alloca i64, align 8
  store ptr %0, ptr %3, align 8
  store i64 %1, ptr %4, align 8
  %6 = load ptr, ptr %3, align 8
  %7 = getelementptr inbounds %struct.CPU, ptr %6, i32 0, i32 0
  %8 = load i64, ptr %4, align 8
  %9 = lshr i64 %8, 2
  %10 = and i64 %9, 31
  %11 = getelementptr inbounds [32 x i64], ptr %7, i64 0, i64 %10
  %12 = load i64, ptr %11, align 8
  %13 = load i64, ptr %4, align 8
  %14 = add i64 %12, %13
  store i64 %14, ptr %5, align 8
  %15 = load i64, ptr %5, align 8
  %16 = mul i64 %15, 37
  %17 = load i64, ptr %5, align 8
  %18 = lshr i64 %17, 7
  %19 = xor i64 %16, %18
  store i64 %19, ptr %5, align 8
  %20 = load i64, ptr %5, align 8
  %21 = load ptr, ptr %3, align 8
  %22 = getelementptr inbounds %struct.CPU, ptr %21, i32 0, i32 0
  %23 = load i64, ptr %4, align 8
  %24 = lshr i64 %23, 2
  %25 = and i64 %24, 31
  %26 = getelementptr inbounds [32 x i64], ptr %22, i64 0, i64 %25
  store i64 %20, ptr %26, align 8
  %27 = load ptr, ptr %3, align 8
  %28 = load i64, ptr %4, align 8
  %29 = add i64 %28, 4
  %30 = call i64 @F125(ptr noundef %27, i64 noundef %29)
  %31 = load i64, ptr %5, align 8
  %32 = add i64 %30, %31
  ret i64 %32
}

; Function Attrs: nounwind
define dso_local i64 @F127(ptr noundef %0, i64 noundef %1) #0 {
  %3 = alloca ptr, align 8
  %4 = alloca i64, align 8
  %5 = alloca i64, align 8
  store ptr %0, ptr %3, align 8
  store i64 %1, ptr %4, align 8
  %6 = load ptr, ptr %3, align 8
  %7 = getelementptr inbounds %struct.CPU, ptr %6, i32 0, i32 0
  %8 = load i64, ptr %4, align 8
  %9 = lshr i64 %8, 2
  %10 = and i64 %9, 31
  %11 = getelementptr inbounds [32 x i64], ptr %7, i64 0, i64 %10
  %12 = load i64, ptr %11, align 8
  %13 = load i64, ptr %4, align 8
  %14 = add i64 %12, %13
  store i64 %14, ptr %5, align 8
  %15 = load i64, ptr %5, align 8
  %16 = mul i64 %15, 37
  %17 = load i64, ptr %5, align 8
  %18 = lshr i64 %17, 7
  %19 = xor i64 %16, %18
  store i64 %19, ptr %5, align 8
  %20 = load i64, ptr %5, align 8
  %21 = load ptr, ptr %3, align 8
  %22 = getelementptr inbounds %struct.CPU, ptr %21, i32 0, i32 0
  %23 = load i64, ptr %4, align 8
  %24 = lshr i64 %23, 2
  %25 = and i64 %24, 31
  %26 = getelementptr inbounds [32 x i64], ptr %22, i64 0, i64 %25
  store i64 %20, ptr %26, align 8
  %27 = load ptr, ptr %3, align 8
  %28 = load i64, ptr %4, align 8
  %29 = add i64 %28, 4
  %30 = call i64 @F126(ptr noundef %27, i64 noundef %29)
  %31 = load i64, ptr %5, align 8
  %32 = add i64 %30, %31
  ret i64 %32
}

; Function Attrs: nounwind
define dso_local i64 @F128(ptr noundef %0, i64 noundef %1) #0 {
  %3 = alloca ptr, align 8
  %4 = alloca i64, align 8
  %5 = alloca i64, align 8
  store ptr %0, ptr %3, align 8
  store i64 %1, ptr %4, align 8
  %6 = load ptr, ptr %3, align 8
  %7 = getelementptr inbounds %struct.CPU, ptr %6, i32 0, i32 0
  %8 = load i64, ptr %4, align 8
  %9 = lshr i64 %8, 2
  %10 = and i64 %9, 31
  %11 = getelementptr inbounds [32 x i64], ptr %7, i64 0, i64 %10
  %12 = load i64, ptr %11, align 8
  %13 = load i64, ptr %4, align 8
  %14 = add i64 %12, %13
  store i64 %14, ptr %5, align 8
  %15 = load i64, ptr %5, align 8
  %16 = mul i64 %15, 37
  %17 = load i64, ptr %5, align 8
  %18 = lshr i64 %17, 7
  %19 = xor i64 %16, %18
  store i64 %19, ptr %5, align 8
  %20 = load i64, ptr %5, align 8
  %21 = load ptr, ptr %3, align 8
  %22 = getelementptr inbounds %struct.CPU, ptr %21, i32 0, i32 0
  %23 = load i64, ptr %4, align 8
  %24 = lshr i64 %23, 2
  %25 = and i64 %24, 31
  %26 = getelementptr inbounds [32 x i64], ptr %22, i64 0, i64 %25
  store i64 %20, ptr %26, align 8
  %27 = load ptr, ptr %3, align 8
  %28 = load i64, ptr %4, align 8
  %29 = add i64 %28, 4
  %30 = call i64 @F127(ptr noundef %27, i64 noundef %29)
  %31 = load i64, ptr %5, align 8
  %32 = add i64 %30, %31
  ret i64 %32
}

; Function Attrs: nounwind
define dso_local i64 @F129(ptr noundef %0, i64 noundef %1) #0 {
  %3 = alloca ptr, align 8
  %4 = alloca i64, align 8
  %5 = alloca i64, align 8
  store ptr %0, ptr %3, align 8
  store i64 %1, ptr %4, align 8
  %6 = load ptr, ptr %3, align 8
  %7 = getelementptr inbounds %struct.CPU, ptr %6, i32 0, i32 0
  %8 = load i64, ptr %4, align 8
  %9 = lshr i64 %8, 2
  %10 = and i64 %9, 31
  %11 = getelementptr inbounds [32 x i64], ptr %7, i64 0, i64 %10
  %12 = load i64, ptr %11, align 8
  %13 = load i64, ptr %4, align 8
  %14 = add i64 %12, %13
  store i64 %14, ptr %5, align 8
  %15 = load i64, ptr %5, align 8
  %16 = mul i64 %15, 37
  %17 = load i64, ptr %5, align 8
  %18 = lshr i64 %17, 7
  %19 = xor i64 %16, %18
  store i64 %19, ptr %5, align 8
  %20 = load i64, ptr %5, align 8
  %21 = load ptr, ptr %3, align 8
  %22 = getelementptr inbounds %struct.CPU, ptr %21, i32 0, i32 0
  %23 = load i64, ptr %4, align 8
  %24 = lshr i64 %23, 2
  %25 = and i64 %24, 31
  %26 = getelementptr inbounds [32 x i64], ptr %22, i64 0, i64 %25
  store i64 %20, ptr %26, align 8
  %27 = load ptr, ptr %3, align 8
  %28 = load i64, ptr %4, align 8
  %29 = add i64 %28, 4
  %30 = call i64 @F128(ptr noundef %27, i64 noundef %29)
  %31 = load i64, ptr %5, align 8
  %32 = add i64 %30, %31
  ret i64 %32
}

; Function Attrs: nounwind
define dso_local i64 @F130(ptr noundef %0, i64 noundef %1) #0 {
  %3 = alloca ptr, align 8
  %4 = alloca i64, align 8
  %5 = alloca i64, align 8
  store ptr %0, ptr %3, align 8
  store i64 %1, ptr %4, align 8
  %6 = load ptr, ptr %3, align 8
  %7 = getelementptr inbounds %struct.CPU, ptr %6, i32 0, i32 0
  %8 = load i64, ptr %4, align 8
  %9 = lshr i64 %8, 2
  %10 = and i64 %9, 31
  %11 = getelementptr inbounds [32 x i64], ptr %7, i64 0, i64 %10
  %12 = load i64, ptr %11, align 8
  %13 = load i64, ptr %4, align 8
  %14 = add i64 %12, %13
  store i64 %14, ptr %5, align 8
  %15 = load i64, ptr %5, align 8
  %16 = mul i64 %15, 37
  %17 = load i64, ptr %5, align 8
  %18 = lshr i64 %17, 7
  %19 = xor i64 %16, %18
  store i64 %19, ptr %5, align 8
  %20 = load i64, ptr %5, align 8
  %21 = load ptr, ptr %3, align 8
  %22 = getelementptr inbounds %struct.CPU, ptr %21, i32 0, i32 0
  %23 = load i64, ptr %4, align 8
  %24 = lshr i64 %23, 2
  %25 = and i64 %24, 31
  %26 = getelementptr inbounds [32 x i64], ptr %22, i64 0, i64 %25
  store i64 %20, ptr %26, align 8
  %27 = load ptr, ptr %3, align 8
  %28 = load i64, ptr %4, align 8
  %29 = add i64 %28, 4
  %30 = call i64 @F129(ptr noundef %27, i64 noundef %29)
  %31 = load i64, ptr %5, align 8
  %32 = add i64 %30, %31
  ret i64 %32
}

; Function Attrs: nounwind
define dso_local i64 @F131(ptr noundef %0, i64 noundef %1) #0 {
  %3 = alloca ptr, align 8
  %4 = alloca i64, align 8
  %5 = alloca i64, align 8
  store ptr %0, ptr %3, align 8
  store i64 %1, ptr %4, align 8
  %6 = load ptr, ptr %3, align 8
  %7 = getelementptr inbounds %struct.CPU, ptr %6, i32 0, i32 0
  %8 = load i64, ptr %4, align 8
  %9 = lshr i64 %8, 2
  %10 = and i64 %9, 31
  %11 = getelementptr inbounds [32 x i64], ptr %7, i64 0, i64 %10
  %12 = load i64, ptr %11, align 8
  %13 = load i64, ptr %4, align 8
  %14 = add i64 %12, %13
  store i64 %14, ptr %5, align 8
  %15 = load i64, ptr %5, align 8
  %16 = mul i64 %15, 37
  %17 = load i64, ptr %5, align 8
  %18 = lshr i64 %17, 7
  %19 = xor i64 %16, %18
  store i64 %19, ptr %5, align 8
  %20 = load i64, ptr %5, align 8
  %21 = load ptr, ptr %3, align 8
  %22 = getelementptr inbounds %struct.CPU, ptr %21, i32 0, i32 0
  %23 = load i64, ptr %4, align 8
  %24 = lshr i64 %23, 2
  %25 = and i64 %24, 31
  %26 = getelementptr inbounds [32 x i64], ptr %22, i64 0, i64 %25
  store i64 %20, ptr %26, align 8
  %27 = load ptr, ptr %3, align 8
  %28 = load i64, ptr %4, align 8
  %29 = add i64 %28, 4
  %30 = call i64 @F130(ptr noundef %27, i64 noundef %29)
  %31 = load i64, ptr %5, align 8
  %32 = add i64 %30, %31
  ret i64 %32
}

; Function Attrs: nounwind
define dso_local i64 @F132(ptr noundef %0, i64 noundef %1) #0 {
  %3 = alloca ptr, align 8
  %4 = alloca i64, align 8
  %5 = alloca i64, align 8
  store ptr %0, ptr %3, align 8
  store i64 %1, ptr %4, align 8
  %6 = load ptr, ptr %3, align 8
  %7 = getelementptr inbounds %struct.CPU, ptr %6, i32 0, i32 0
  %8 = load i64, ptr %4, align 8
  %9 = lshr i64 %8, 2
  %10 = and i64 %9, 31
  %11 = getelementptr inbounds [32 x i64], ptr %7, i64 0, i64 %10
  %12 = load i64, ptr %11, align 8
  %13 = load i64, ptr %4, align 8
  %14 = add i64 %12, %13
  store i64 %14, ptr %5, align 8
  %15 = load i64, ptr %5, align 8
  %16 = mul i64 %15, 37
  %17 = load i64, ptr %5, align 8
  %18 = lshr i64 %17, 7
  %19 = xor i64 %16, %18
  store i64 %19, ptr %5, align 8
  %20 = load i64, ptr %5, align 8
  %21 = load ptr, ptr %3, align 8
  %22 = getelementptr inbounds %struct.CPU, ptr %21, i32 0, i32 0
  %23 = load i64, ptr %4, align 8
  %24 = lshr i64 %23, 2
  %25 = and i64 %24, 31
  %26 = getelementptr inbounds [32 x i64], ptr %22, i64 0, i64 %25
  store i64 %20, ptr %26, align 8
  %27 = load ptr, ptr %3, align 8
  %28 = load i64, ptr %4, align 8
  %29 = add i64 %28, 4
  %30 = call i64 @F131(ptr noundef %27, i64 noundef %29)
  %31 = load i64, ptr %5, align 8
  %32 = add i64 %30, %31
  ret i64 %32
}

; Function Attrs: nounwind
define dso_local i64 @F133(ptr noundef %0, i64 noundef %1) #0 {
  %3 = alloca ptr, align 8
  %4 = alloca i64, align 8
  %5 = alloca i64, align 8
  store ptr %0, ptr %3, align 8
  store i64 %1, ptr %4, align 8
  %6 = load ptr, ptr %3, align 8
  %7 = getelementptr inbounds %struct.CPU, ptr %6, i32 0, i32 0
  %8 = load i64, ptr %4, align 8
  %9 = lshr i64 %8, 2
  %10 = and i64 %9, 31
  %11 = getelementptr inbounds [32 x i64], ptr %7, i64 0, i64 %10
  %12 = load i64, ptr %11, align 8
  %13 = load i64, ptr %4, align 8
  %14 = add i64 %12, %13
  store i64 %14, ptr %5, align 8
  %15 = load i64, ptr %5, align 8
  %16 = mul i64 %15, 37
  %17 = load i64, ptr %5, align 8
  %18 = lshr i64 %17, 7
  %19 = xor i64 %16, %18
  store i64 %19, ptr %5, align 8
  %20 = load i64, ptr %5, align 8
  %21 = load ptr, ptr %3, align 8
  %22 = getelementptr inbounds %struct.CPU, ptr %21, i32 0, i32 0
  %23 = load i64, ptr %4, align 8
  %24 = lshr i64 %23, 2
  %25 = and i64 %24, 31
  %26 = getelementptr inbounds [32 x i64], ptr %22, i64 0, i64 %25
  store i64 %20, ptr %26, align 8
  %27 = load ptr, ptr %3, align 8
  %28 = load i64, ptr %4, align 8
  %29 = add i64 %28, 4
  %30 = call i64 @F132(ptr noundef %27, i64 noundef %29)
  %31 = load i64, ptr %5, align 8
  %32 = add i64 %30, %31
  ret i64 %32
}

; Function Attrs: nounwind
define dso_local i64 @F134(ptr noundef %0, i64 noundef %1) #0 {
  %3 = alloca ptr, align 8
  %4 = alloca i64, align 8
  %5 = alloca i64, align 8
  store ptr %0, ptr %3, align 8
  store i64 %1, ptr %4, align 8
  %6 = load ptr, ptr %3, align 8
  %7 = getelementptr inbounds %struct.CPU, ptr %6, i32 0, i32 0
  %8 = load i64, ptr %4, align 8
  %9 = lshr i64 %8, 2
  %10 = and i64 %9, 31
  %11 = getelementptr inbounds [32 x i64], ptr %7, i64 0, i64 %10
  %12 = load i64, ptr %11, align 8
  %13 = load i64, ptr %4, align 8
  %14 = add i64 %12, %13
  store i64 %14, ptr %5, align 8
  %15 = load i64, ptr %5, align 8
  %16 = mul i64 %15, 37
  %17 = load i64, ptr %5, align 8
  %18 = lshr i64 %17, 7
  %19 = xor i64 %16, %18
  store i64 %19, ptr %5, align 8
  %20 = load i64, ptr %5, align 8
  %21 = load ptr, ptr %3, align 8
  %22 = getelementptr inbounds %struct.CPU, ptr %21, i32 0, i32 0
  %23 = load i64, ptr %4, align 8
  %24 = lshr i64 %23, 2
  %25 = and i64 %24, 31
  %26 = getelementptr inbounds [32 x i64], ptr %22, i64 0, i64 %25
  store i64 %20, ptr %26, align 8
  %27 = load ptr, ptr %3, align 8
  %28 = load i64, ptr %4, align 8
  %29 = add i64 %28, 4
  %30 = call i64 @F133(ptr noundef %27, i64 noundef %29)
  %31 = load i64, ptr %5, align 8
  %32 = add i64 %30, %31
  ret i64 %32
}

; Function Attrs: nounwind
define dso_local i64 @F135(ptr noundef %0, i64 noundef %1) #0 {
  %3 = alloca ptr, align 8
  %4 = alloca i64, align 8
  %5 = alloca i64, align 8
  store ptr %0, ptr %3, align 8
  store i64 %1, ptr %4, align 8
  %6 = load ptr, ptr %3, align 8
  %7 = getelementptr inbounds %struct.CPU, ptr %6, i32 0, i32 0
  %8 = load i64, ptr %4, align 8
  %9 = lshr i64 %8, 2
  %10 = and i64 %9, 31
  %11 = getelementptr inbounds [32 x i64], ptr %7, i64 0, i64 %10
  %12 = load i64, ptr %11, align 8
  %13 = load i64, ptr %4, align 8
  %14 = add i64 %12, %13
  store i64 %14, ptr %5, align 8
  %15 = load i64, ptr %5, align 8
  %16 = mul i64 %15, 37
  %17 = load i64, ptr %5, align 8
  %18 = lshr i64 %17, 7
  %19 = xor i64 %16, %18
  store i64 %19, ptr %5, align 8
  %20 = load i64, ptr %5, align 8
  %21 = load ptr, ptr %3, align 8
  %22 = getelementptr inbounds %struct.CPU, ptr %21, i32 0, i32 0
  %23 = load i64, ptr %4, align 8
  %24 = lshr i64 %23, 2
  %25 = and i64 %24, 31
  %26 = getelementptr inbounds [32 x i64], ptr %22, i64 0, i64 %25
  store i64 %20, ptr %26, align 8
  %27 = load ptr, ptr %3, align 8
  %28 = load i64, ptr %4, align 8
  %29 = add i64 %28, 4
  %30 = call i64 @F134(ptr noundef %27, i64 noundef %29)
  %31 = load i64, ptr %5, align 8
  %32 = add i64 %30, %31
  ret i64 %32
}

; Function Attrs: nounwind
define dso_local i64 @F136(ptr noundef %0, i64 noundef %1) #0 {
  %3 = alloca ptr, align 8
  %4 = alloca i64, align 8
  %5 = alloca i64, align 8
  store ptr %0, ptr %3, align 8
  store i64 %1, ptr %4, align 8
  %6 = load ptr, ptr %3, align 8
  %7 = getelementptr inbounds %struct.CPU, ptr %6, i32 0, i32 0
  %8 = load i64, ptr %4, align 8
  %9 = lshr i64 %8, 2
  %10 = and i64 %9, 31
  %11 = getelementptr inbounds [32 x i64], ptr %7, i64 0, i64 %10
  %12 = load i64, ptr %11, align 8
  %13 = load i64, ptr %4, align 8
  %14 = add i64 %12, %13
  store i64 %14, ptr %5, align 8
  %15 = load i64, ptr %5, align 8
  %16 = mul i64 %15, 37
  %17 = load i64, ptr %5, align 8
  %18 = lshr i64 %17, 7
  %19 = xor i64 %16, %18
  store i64 %19, ptr %5, align 8
  %20 = load i64, ptr %5, align 8
  %21 = load ptr, ptr %3, align 8
  %22 = getelementptr inbounds %struct.CPU, ptr %21, i32 0, i32 0
  %23 = load i64, ptr %4, align 8
  %24 = lshr i64 %23, 2
  %25 = and i64 %24, 31
  %26 = getelementptr inbounds [32 x i64], ptr %22, i64 0, i64 %25
  store i64 %20, ptr %26, align 8
  %27 = load ptr, ptr %3, align 8
  %28 = load i64, ptr %4, align 8
  %29 = add i64 %28, 4
  %30 = call i64 @F135(ptr noundef %27, i64 noundef %29)
  %31 = load i64, ptr %5, align 8
  %32 = add i64 %30, %31
  ret i64 %32
}

; Function Attrs: nounwind
define dso_local i64 @F137(ptr noundef %0, i64 noundef %1) #0 {
  %3 = alloca ptr, align 8
  %4 = alloca i64, align 8
  %5 = alloca i64, align 8
  store ptr %0, ptr %3, align 8
  store i64 %1, ptr %4, align 8
  %6 = load ptr, ptr %3, align 8
  %7 = getelementptr inbounds %struct.CPU, ptr %6, i32 0, i32 0
  %8 = load i64, ptr %4, align 8
  %9 = lshr i64 %8, 2
  %10 = and i64 %9, 31
  %11 = getelementptr inbounds [32 x i64], ptr %7, i64 0, i64 %10
  %12 = load i64, ptr %11, align 8
  %13 = load i64, ptr %4, align 8
  %14 = add i64 %12, %13
  store i64 %14, ptr %5, align 8
  %15 = load i64, ptr %5, align 8
  %16 = mul i64 %15, 37
  %17 = load i64, ptr %5, align 8
  %18 = lshr i64 %17, 7
  %19 = xor i64 %16, %18
  store i64 %19, ptr %5, align 8
  %20 = load i64, ptr %5, align 8
  %21 = load ptr, ptr %3, align 8
  %22 = getelementptr inbounds %struct.CPU, ptr %21, i32 0, i32 0
  %23 = load i64, ptr %4, align 8
  %24 = lshr i64 %23, 2
  %25 = and i64 %24, 31
  %26 = getelementptr inbounds [32 x i64], ptr %22, i64 0, i64 %25
  store i64 %20, ptr %26, align 8
  %27 = load ptr, ptr %3, align 8
  %28 = load i64, ptr %4, align 8
  %29 = add i64 %28, 4
  %30 = call i64 @F136(ptr noundef %27, i64 noundef %29)
  %31 = load i64, ptr %5, align 8
  %32 = add i64 %30, %31
  ret i64 %32
}

; Function Attrs: nounwind
define dso_local i64 @F138(ptr noundef %0, i64 noundef %1) #0 {
  %3 = alloca ptr, align 8
  %4 = alloca i64, align 8
  %5 = alloca i64, align 8
  store ptr %0, ptr %3, align 8
  store i64 %1, ptr %4, align 8
  %6 = load ptr, ptr %3, align 8
  %7 = getelementptr inbounds %struct.CPU, ptr %6, i32 0, i32 0
  %8 = load i64, ptr %4, align 8
  %9 = lshr i64 %8, 2
  %10 = and i64 %9, 31
  %11 = getelementptr inbounds [32 x i64], ptr %7, i64 0, i64 %10
  %12 = load i64, ptr %11, align 8
  %13 = load i64, ptr %4, align 8
  %14 = add i64 %12, %13
  store i64 %14, ptr %5, align 8
  %15 = load i64, ptr %5, align 8
  %16 = mul i64 %15, 37
  %17 = load i64, ptr %5, align 8
  %18 = lshr i64 %17, 7
  %19 = xor i64 %16, %18
  store i64 %19, ptr %5, align 8
  %20 = load i64, ptr %5, align 8
  %21 = load ptr, ptr %3, align 8
  %22 = getelementptr inbounds %struct.CPU, ptr %21, i32 0, i32 0
  %23 = load i64, ptr %4, align 8
  %24 = lshr i64 %23, 2
  %25 = and i64 %24, 31
  %26 = getelementptr inbounds [32 x i64], ptr %22, i64 0, i64 %25
  store i64 %20, ptr %26, align 8
  %27 = load ptr, ptr %3, align 8
  %28 = load i64, ptr %4, align 8
  %29 = add i64 %28, 4
  %30 = call i64 @F137(ptr noundef %27, i64 noundef %29)
  %31 = load i64, ptr %5, align 8
  %32 = add i64 %30, %31
  ret i64 %32
}

; Function Attrs: nounwind
define dso_local i64 @F139(ptr noundef %0, i64 noundef %1) #0 {
  %3 = alloca ptr, align 8
  %4 = alloca i64, align 8
  %5 = alloca i64, align 8
  store ptr %0, ptr %3, align 8
  store i64 %1, ptr %4, align 8
  %6 = load ptr, ptr %3, align 8
  %7 = getelementptr inbounds %struct.CPU, ptr %6, i32 0, i32 0
  %8 = load i64, ptr %4, align 8
  %9 = lshr i64 %8, 2
  %10 = and i64 %9, 31
  %11 = getelementptr inbounds [32 x i64], ptr %7, i64 0, i64 %10
  %12 = load i64, ptr %11, align 8
  %13 = load i64, ptr %4, align 8
  %14 = add i64 %12, %13
  store i64 %14, ptr %5, align 8
  %15 = load i64, ptr %5, align 8
  %16 = mul i64 %15, 37
  %17 = load i64, ptr %5, align 8
  %18 = lshr i64 %17, 7
  %19 = xor i64 %16, %18
  store i64 %19, ptr %5, align 8
  %20 = load i64, ptr %5, align 8
  %21 = load ptr, ptr %3, align 8
  %22 = getelementptr inbounds %struct.CPU, ptr %21, i32 0, i32 0
  %23 = load i64, ptr %4, align 8
  %24 = lshr i64 %23, 2
  %25 = and i64 %24, 31
  %26 = getelementptr inbounds [32 x i64], ptr %22, i64 0, i64 %25
  store i64 %20, ptr %26, align 8
  %27 = load ptr, ptr %3, align 8
  %28 = load i64, ptr %4, align 8
  %29 = add i64 %28, 4
  %30 = call i64 @F138(ptr noundef %27, i64 noundef %29)
  %31 = load i64, ptr %5, align 8
  %32 = add i64 %30, %31
  ret i64 %32
}

; Function Attrs: nounwind
define dso_local i64 @F140(ptr noundef %0, i64 noundef %1) #0 {
  %3 = alloca ptr, align 8
  %4 = alloca i64, align 8
  %5 = alloca i64, align 8
  store ptr %0, ptr %3, align 8
  store i64 %1, ptr %4, align 8
  %6 = load ptr, ptr %3, align 8
  %7 = getelementptr inbounds %struct.CPU, ptr %6, i32 0, i32 0
  %8 = load i64, ptr %4, align 8
  %9 = lshr i64 %8, 2
  %10 = and i64 %9, 31
  %11 = getelementptr inbounds [32 x i64], ptr %7, i64 0, i64 %10
  %12 = load i64, ptr %11, align 8
  %13 = load i64, ptr %4, align 8
  %14 = add i64 %12, %13
  store i64 %14, ptr %5, align 8
  %15 = load i64, ptr %5, align 8
  %16 = mul i64 %15, 37
  %17 = load i64, ptr %5, align 8
  %18 = lshr i64 %17, 7
  %19 = xor i64 %16, %18
  store i64 %19, ptr %5, align 8
  %20 = load i64, ptr %5, align 8
  %21 = load ptr, ptr %3, align 8
  %22 = getelementptr inbounds %struct.CPU, ptr %21, i32 0, i32 0
  %23 = load i64, ptr %4, align 8
  %24 = lshr i64 %23, 2
  %25 = and i64 %24, 31
  %26 = getelementptr inbounds [32 x i64], ptr %22, i64 0, i64 %25
  store i64 %20, ptr %26, align 8
  %27 = load ptr, ptr %3, align 8
  %28 = load i64, ptr %4, align 8
  %29 = add i64 %28, 4
  %30 = call i64 @F139(ptr noundef %27, i64 noundef %29)
  %31 = load i64, ptr %5, align 8
  %32 = add i64 %30, %31
  ret i64 %32
}

; Function Attrs: nounwind
define dso_local i64 @F141(ptr noundef %0, i64 noundef %1) #0 {
  %3 = alloca ptr, align 8
  %4 = alloca i64, align 8
  %5 = alloca i64, align 8
  store ptr %0, ptr %3, align 8
  store i64 %1, ptr %4, align 8
  %6 = load ptr, ptr %3, align 8
  %7 = getelementptr inbounds %struct.CPU, ptr %6, i32 0, i32 0
  %8 = load i64, ptr %4, align 8
  %9 = lshr i64 %8, 2
  %10 = and i64 %9, 31
  %11 = getelementptr inbounds [32 x i64], ptr %7, i64 0, i64 %10
  %12 = load i64, ptr %11, align 8
  %13 = load i64, ptr %4, align 8
  %14 = add i64 %12, %13
  store i64 %14, ptr %5, align 8
  %15 = load i64, ptr %5, align 8
  %16 = mul i64 %15, 37
  %17 = load i64, ptr %5, align 8
  %18 = lshr i64 %17, 7
  %19 = xor i64 %16, %18
  store i64 %19, ptr %5, align 8
  %20 = load i64, ptr %5, align 8
  %21 = load ptr, ptr %3, align 8
  %22 = getelementptr inbounds %struct.CPU, ptr %21, i32 0, i32 0
  %23 = load i64, ptr %4, align 8
  %24 = lshr i64 %23, 2
  %25 = and i64 %24, 31
  %26 = getelementptr inbounds [32 x i64], ptr %22, i64 0, i64 %25
  store i64 %20, ptr %26, align 8
  %27 = load ptr, ptr %3, align 8
  %28 = load i64, ptr %4, align 8
  %29 = add i64 %28, 4
  %30 = call i64 @F140(ptr noundef %27, i64 noundef %29)
  %31 = load i64, ptr %5, align 8
  %32 = add i64 %30, %31
  ret i64 %32
}

; Function Attrs: nounwind
define dso_local i64 @F142(ptr noundef %0, i64 noundef %1) #0 {
  %3 = alloca ptr, align 8
  %4 = alloca i64, align 8
  %5 = alloca i64, align 8
  store ptr %0, ptr %3, align 8
  store i64 %1, ptr %4, align 8
  %6 = load ptr, ptr %3, align 8
  %7 = getelementptr inbounds %struct.CPU, ptr %6, i32 0, i32 0
  %8 = load i64, ptr %4, align 8
  %9 = lshr i64 %8, 2
  %10 = and i64 %9, 31
  %11 = getelementptr inbounds [32 x i64], ptr %7, i64 0, i64 %10
  %12 = load i64, ptr %11, align 8
  %13 = load i64, ptr %4, align 8
  %14 = add i64 %12, %13
  store i64 %14, ptr %5, align 8
  %15 = load i64, ptr %5, align 8
  %16 = mul i64 %15, 37
  %17 = load i64, ptr %5, align 8
  %18 = lshr i64 %17, 7
  %19 = xor i64 %16, %18
  store i64 %19, ptr %5, align 8
  %20 = load i64, ptr %5, align 8
  %21 = load ptr, ptr %3, align 8
  %22 = getelementptr inbounds %struct.CPU, ptr %21, i32 0, i32 0
  %23 = load i64, ptr %4, align 8
  %24 = lshr i64 %23, 2
  %25 = and i64 %24, 31
  %26 = getelementptr inbounds [32 x i64], ptr %22, i64 0, i64 %25
  store i64 %20, ptr %26, align 8
  %27 = load ptr, ptr %3, align 8
  %28 = load i64, ptr %4, align 8
  %29 = add i64 %28, 4
  %30 = call i64 @F141(ptr noundef %27, i64 noundef %29)
  %31 = load i64, ptr %5, align 8
  %32 = add i64 %30, %31
  ret i64 %32
}

; Function Attrs: nounwind
define dso_local i64 @F143(ptr noundef %0, i64 noundef %1) #0 {
  %3 = alloca ptr, align 8
  %4 = alloca i64, align 8
  %5 = alloca i64, align 8
  store ptr %0, ptr %3, align 8
  store i64 %1, ptr %4, align 8
  %6 = load ptr, ptr %3, align 8
  %7 = getelementptr inbounds %struct.CPU, ptr %6, i32 0, i32 0
  %8 = load i64, ptr %4, align 8
  %9 = lshr i64 %8, 2
  %10 = and i64 %9, 31
  %11 = getelementptr inbounds [32 x i64], ptr %7, i64 0, i64 %10
  %12 = load i64, ptr %11, align 8
  %13 = load i64, ptr %4, align 8
  %14 = add i64 %12, %13
  store i64 %14, ptr %5, align 8
  %15 = load i64, ptr %5, align 8
  %16 = mul i64 %15, 37
  %17 = load i64, ptr %5, align 8
  %18 = lshr i64 %17, 7
  %19 = xor i64 %16, %18
  store i64 %19, ptr %5, align 8
  %20 = load i64, ptr %5, align 8
  %21 = load ptr, ptr %3, align 8
  %22 = getelementptr inbounds %struct.CPU, ptr %21, i32 0, i32 0
  %23 = load i64, ptr %4, align 8
  %24 = lshr i64 %23, 2
  %25 = and i64 %24, 31
  %26 = getelementptr inbounds [32 x i64], ptr %22, i64 0, i64 %25
  store i64 %20, ptr %26, align 8
  %27 = load ptr, ptr %3, align 8
  %28 = load i64, ptr %4, align 8
  %29 = add i64 %28, 4
  %30 = call i64 @F142(ptr noundef %27, i64 noundef %29)
  %31 = load i64, ptr %5, align 8
  %32 = add i64 %30, %31
  ret i64 %32
}

; Function Attrs: nounwind
define dso_local i64 @F144(ptr noundef %0, i64 noundef %1) #0 {
  %3 = alloca ptr, align 8
  %4 = alloca i64, align 8
  %5 = alloca i64, align 8
  store ptr %0, ptr %3, align 8
  store i64 %1, ptr %4, align 8
  %6 = load ptr, ptr %3, align 8
  %7 = getelementptr inbounds %struct.CPU, ptr %6, i32 0, i32 0
  %8 = load i64, ptr %4, align 8
  %9 = lshr i64 %8, 2
  %10 = and i64 %9, 31
  %11 = getelementptr inbounds [32 x i64], ptr %7, i64 0, i64 %10
  %12 = load i64, ptr %11, align 8
  %13 = load i64, ptr %4, align 8
  %14 = add i64 %12, %13
  store i64 %14, ptr %5, align 8
  %15 = load i64, ptr %5, align 8
  %16 = mul i64 %15, 37
  %17 = load i64, ptr %5, align 8
  %18 = lshr i64 %17, 7
  %19 = xor i64 %16, %18
  store i64 %19, ptr %5, align 8
  %20 = load i64, ptr %5, align 8
  %21 = load ptr, ptr %3, align 8
  %22 = getelementptr inbounds %struct.CPU, ptr %21, i32 0, i32 0
  %23 = load i64, ptr %4, align 8
  %24 = lshr i64 %23, 2
  %25 = and i64 %24, 31
  %26 = getelementptr inbounds [32 x i64], ptr %22, i64 0, i64 %25
  store i64 %20, ptr %26, align 8
  %27 = load ptr, ptr %3, align 8
  %28 = load i64, ptr %4, align 8
  %29 = add i64 %28, 4
  %30 = call i64 @F143(ptr noundef %27, i64 noundef %29)
  %31 = load i64, ptr %5, align 8
  %32 = add i64 %30, %31
  ret i64 %32
}

; Function Attrs: nounwind
define dso_local i64 @F145(ptr noundef %0, i64 noundef %1) #0 {
  %3 = alloca ptr, align 8
  %4 = alloca i64, align 8
  %5 = alloca i64, align 8
  store ptr %0, ptr %3, align 8
  store i64 %1, ptr %4, align 8
  %6 = load ptr, ptr %3, align 8
  %7 = getelementptr inbounds %struct.CPU, ptr %6, i32 0, i32 0
  %8 = load i64, ptr %4, align 8
  %9 = lshr i64 %8, 2
  %10 = and i64 %9, 31
  %11 = getelementptr inbounds [32 x i64], ptr %7, i64 0, i64 %10
  %12 = load i64, ptr %11, align 8
  %13 = load i64, ptr %4, align 8
  %14 = add i64 %12, %13
  store i64 %14, ptr %5, align 8
  %15 = load i64, ptr %5, align 8
  %16 = mul i64 %15, 37
  %17 = load i64, ptr %5, align 8
  %18 = lshr i64 %17, 7
  %19 = xor i64 %16, %18
  store i64 %19, ptr %5, align 8
  %20 = load i64, ptr %5, align 8
  %21 = load ptr, ptr %3, align 8
  %22 = getelementptr inbounds %struct.CPU, ptr %21, i32 0, i32 0
  %23 = load i64, ptr %4, align 8
  %24 = lshr i64 %23, 2
  %25 = and i64 %24, 31
  %26 = getelementptr inbounds [32 x i64], ptr %22, i64 0, i64 %25
  store i64 %20, ptr %26, align 8
  %27 = load ptr, ptr %3, align 8
  %28 = load i64, ptr %4, align 8
  %29 = add i64 %28, 4
  %30 = call i64 @F144(ptr noundef %27, i64 noundef %29)
  %31 = load i64, ptr %5, align 8
  %32 = add i64 %30, %31
  ret i64 %32
}

; Function Attrs: nounwind
define dso_local i64 @F146(ptr noundef %0, i64 noundef %1) #0 {
  %3 = alloca ptr, align 8
  %4 = alloca i64, align 8
  %5 = alloca i64, align 8
  store ptr %0, ptr %3, align 8
  store i64 %1, ptr %4, align 8
  %6 = load ptr, ptr %3, align 8
  %7 = getelementptr inbounds %struct.CPU, ptr %6, i32 0, i32 0
  %8 = load i64, ptr %4, align 8
  %9 = lshr i64 %8, 2
  %10 = and i64 %9, 31
  %11 = getelementptr inbounds [32 x i64], ptr %7, i64 0, i64 %10
  %12 = load i64, ptr %11, align 8
  %13 = load i64, ptr %4, align 8
  %14 = add i64 %12, %13
  store i64 %14, ptr %5, align 8
  %15 = load i64, ptr %5, align 8
  %16 = mul i64 %15, 37
  %17 = load i64, ptr %5, align 8
  %18 = lshr i64 %17, 7
  %19 = xor i64 %16, %18
  store i64 %19, ptr %5, align 8
  %20 = load i64, ptr %5, align 8
  %21 = load ptr, ptr %3, align 8
  %22 = getelementptr inbounds %struct.CPU, ptr %21, i32 0, i32 0
  %23 = load i64, ptr %4, align 8
  %24 = lshr i64 %23, 2
  %25 = and i64 %24, 31
  %26 = getelementptr inbounds [32 x i64], ptr %22, i64 0, i64 %25
  store i64 %20, ptr %26, align 8
  %27 = load ptr, ptr %3, align 8
  %28 = load i64, ptr %4, align 8
  %29 = add i64 %28, 4
  %30 = call i64 @F145(ptr noundef %27, i64 noundef %29)
  %31 = load i64, ptr %5, align 8
  %32 = add i64 %30, %31
  ret i64 %32
}

; Function Attrs: nounwind
define dso_local i64 @F147(ptr noundef %0, i64 noundef %1) #0 {
  %3 = alloca ptr, align 8
  %4 = alloca i64, align 8
  %5 = alloca i64, align 8
  store ptr %0, ptr %3, align 8
  store i64 %1, ptr %4, align 8
  %6 = load ptr, ptr %3, align 8
  %7 = getelementptr inbounds %struct.CPU, ptr %6, i32 0, i32 0
  %8 = load i64, ptr %4, align 8
  %9 = lshr i64 %8, 2
  %10 = and i64 %9, 31
  %11 = getelementptr inbounds [32 x i64], ptr %7, i64 0, i64 %10
  %12 = load i64, ptr %11, align 8
  %13 = load i64, ptr %4, align 8
  %14 = add i64 %12, %13
  store i64 %14, ptr %5, align 8
  %15 = load i64, ptr %5, align 8
  %16 = mul i64 %15, 37
  %17 = load i64, ptr %5, align 8
  %18 = lshr i64 %17, 7
  %19 = xor i64 %16, %18
  store i64 %19, ptr %5, align 8
  %20 = load i64, ptr %5, align 8
  %21 = load ptr, ptr %3, align 8
  %22 = getelementptr inbounds %struct.CPU, ptr %21, i32 0, i32 0
  %23 = load i64, ptr %4, align 8
  %24 = lshr i64 %23, 2
  %25 = and i64 %24, 31
  %26 = getelementptr inbounds [32 x i64], ptr %22, i64 0, i64 %25
  store i64 %20, ptr %26, align 8
  %27 = load ptr, ptr %3, align 8
  %28 = load i64, ptr %4, align 8
  %29 = add i64 %28, 4
  %30 = call i64 @F146(ptr noundef %27, i64 noundef %29)
  %31 = load i64, ptr %5, align 8
  %32 = add i64 %30, %31
  ret i64 %32
}

; Function Attrs: nounwind
define dso_local i64 @F148(ptr noundef %0, i64 noundef %1) #0 {
  %3 = alloca ptr, align 8
  %4 = alloca i64, align 8
  %5 = alloca i64, align 8
  store ptr %0, ptr %3, align 8
  store i64 %1, ptr %4, align 8
  %6 = load ptr, ptr %3, align 8
  %7 = getelementptr inbounds %struct.CPU, ptr %6, i32 0, i32 0
  %8 = load i64, ptr %4, align 8
  %9 = lshr i64 %8, 2
  %10 = and i64 %9, 31
  %11 = getelementptr inbounds [32 x i64], ptr %7, i64 0, i64 %10
  %12 = load i64, ptr %11, align 8
  %13 = load i64, ptr %4, align 8
  %14 = add i64 %12, %13
  store i64 %14, ptr %5, align 8
  %15 = load i64, ptr %5, align 8
  %16 = mul i64 %15, 37
  %17 = load i64, ptr %5, align 8
  %18 = lshr i64 %17, 7
  %19 = xor i64 %16, %18
  store i64 %19, ptr %5, align 8
  %20 = load i64, ptr %5, align 8
  %21 = load ptr, ptr %3, align 8
  %22 = getelementptr inbounds %struct.CPU, ptr %21, i32 0, i32 0
  %23 = load i64, ptr %4, align 8
  %24 = lshr i64 %23, 2
  %25 = and i64 %24, 31
  %26 = getelementptr inbounds [32 x i64], ptr %22, i64 0, i64 %25
  store i64 %20, ptr %26, align 8
  %27 = load ptr, ptr %3, align 8
  %28 = load i64, ptr %4, align 8
  %29 = add i64 %28, 4
  %30 = call i64 @F147(ptr noundef %27, i64 noundef %29)
  %31 = load i64, ptr %5, align 8
  %32 = add i64 %30, %31
  ret i64 %32
}

; Function Attrs: nounwind
define dso_local i64 @F149(ptr noundef %0, i64 noundef %1) #0 {
  %3 = alloca ptr, align 8
  %4 = alloca i64, align 8
  %5 = alloca i64, align 8
  store ptr %0, ptr %3, align 8
  store i64 %1, ptr %4, align 8
  %6 = load ptr, ptr %3, align 8
  %7 = getelementptr inbounds %struct.CPU, ptr %6, i32 0, i32 0
  %8 = load i64, ptr %4, align 8
  %9 = lshr i64 %8, 2
  %10 = and i64 %9, 31
  %11 = getelementptr inbounds [32 x i64], ptr %7, i64 0, i64 %10
  %12 = load i64, ptr %11, align 8
  %13 = load i64, ptr %4, align 8
  %14 = add i64 %12, %13
  store i64 %14, ptr %5, align 8
  %15 = load i64, ptr %5, align 8
  %16 = mul i64 %15, 37
  %17 = load i64, ptr %5, align 8
  %18 = lshr i64 %17, 7
  %19 = xor i64 %16, %18
  store i64 %19, ptr %5, align 8
  %20 = load i64, ptr %5, align 8
  %21 = load ptr, ptr %3, align 8
  %22 = getelementptr inbounds %struct.CPU, ptr %21, i32 0, i32 0
  %23 = load i64, ptr %4, align 8
  %24 = lshr i64 %23, 2
  %25 = and i64 %24, 31
  %26 = getelementptr inbounds [32 x i64], ptr %22, i64 0, i64 %25
  store i64 %20, ptr %26, align 8
  %27 = load ptr, ptr %3, align 8
  %28 = load i64, ptr %4, align 8
  %29 = add i64 %28, 4
  %30 = call i64 @F148(ptr noundef %27, i64 noundef %29)
  %31 = load i64, ptr %5, align 8
  %32 = add i64 %30, %31
  ret i64 %32
}

; Function Attrs: nounwind
define dso_local i64 @F150(ptr noundef %0, i64 noundef %1) #0 {
  %3 = alloca ptr, align 8
  %4 = alloca i64, align 8
  %5 = alloca i64, align 8
  store ptr %0, ptr %3, align 8
  store i64 %1, ptr %4, align 8
  %6 = load ptr, ptr %3, align 8
  %7 = getelementptr inbounds %struct.CPU, ptr %6, i32 0, i32 0
  %8 = load i64, ptr %4, align 8
  %9 = lshr i64 %8, 2
  %10 = and i64 %9, 31
  %11 = getelementptr inbounds [32 x i64], ptr %7, i64 0, i64 %10
  %12 = load i64, ptr %11, align 8
  %13 = load i64, ptr %4, align 8
  %14 = add i64 %12, %13
  store i64 %14, ptr %5, align 8
  %15 = load i64, ptr %5, align 8
  %16 = mul i64 %15, 37
  %17 = load i64, ptr %5, align 8
  %18 = lshr i64 %17, 7
  %19 = xor i64 %16, %18
  store i64 %19, ptr %5, align 8
  %20 = load i64, ptr %5, align 8
  %21 = load ptr, ptr %3, align 8
  %22 = getelementptr inbounds %struct.CPU, ptr %21, i32 0, i32 0
  %23 = load i64, ptr %4, align 8
  %24 = lshr i64 %23, 2
  %25 = and i64 %24, 31
  %26 = getelementptr inbounds [32 x i64], ptr %22, i64 0, i64 %25
  store i64 %20, ptr %26, align 8
  %27 = load ptr, ptr %3, align 8
  %28 = load i64, ptr %4, align 8
  %29 = add i64 %28, 4
  %30 = call i64 @F149(ptr noundef %27, i64 noundef %29)
  %31 = load i64, ptr %5, align 8
  %32 = add i64 %30, %31
  ret i64 %32
}

; Function Attrs: nounwind
define dso_local i64 @F151(ptr noundef %0, i64 noundef %1) #0 {
  %3 = alloca ptr, align 8
  %4 = alloca i64, align 8
  %5 = alloca i64, align 8
  store ptr %0, ptr %3, align 8
  store i64 %1, ptr %4, align 8
  %6 = load ptr, ptr %3, align 8
  %7 = getelementptr inbounds %struct.CPU, ptr %6, i32 0, i32 0
  %8 = load i64, ptr %4, align 8
  %9 = lshr i64 %8, 2
  %10 = and i64 %9, 31
  %11 = getelementptr inbounds [32 x i64], ptr %7, i64 0, i64 %10
  %12 = load i64, ptr %11, align 8
  %13 = load i64, ptr %4, align 8
  %14 = add i64 %12, %13
  store i64 %14, ptr %5, align 8
  %15 = load i64, ptr %5, align 8
  %16 = mul i64 %15, 37
  %17 = load i64, ptr %5, align 8
  %18 = lshr i64 %17, 7
  %19 = xor i64 %16, %18
  store i64 %19, ptr %5, align 8
  %20 = load i64, ptr %5, align 8
  %21 = load ptr, ptr %3, align 8
  %22 = getelementptr inbounds %struct.CPU, ptr %21, i32 0, i32 0
  %23 = load i64, ptr %4, align 8
  %24 = lshr i64 %23, 2
  %25 = and i64 %24, 31
  %26 = getelementptr inbounds [32 x i64], ptr %22, i64 0, i64 %25
  store i64 %20, ptr %26, align 8
  %27 = load ptr, ptr %3, align 8
  %28 = load i64, ptr %4, align 8
  %29 = add i64 %28, 4
  %30 = call i64 @F150(ptr noundef %27, i64 noundef %29)
  %31 = load i64, ptr %5, align 8
  %32 = add i64 %30, %31
  ret i64 %32
}

; Function Attrs: nounwind
define dso_local i64 @F152(ptr noundef %0, i64 noundef %1) #0 {
  %3 = alloca ptr, align 8
  %4 = alloca i64, align 8
  %5 = alloca i64, align 8
  store ptr %0, ptr %3, align 8
  store i64 %1, ptr %4, align 8
  %6 = load ptr, ptr %3, align 8
  %7 = getelementptr inbounds %struct.CPU, ptr %6, i32 0, i32 0
  %8 = load i64, ptr %4, align 8
  %9 = lshr i64 %8, 2
  %10 = and i64 %9, 31
  %11 = getelementptr inbounds [32 x i64], ptr %7, i64 0, i64 %10
  %12 = load i64, ptr %11, align 8
  %13 = load i64, ptr %4, align 8
  %14 = add i64 %12, %13
  store i64 %14, ptr %5, align 8
  %15 = load i64, ptr %5, align 8
  %16 = mul i64 %15, 37
  %17 = load i64, ptr %5, align 8
  %18 = lshr i64 %17, 7
  %19 = xor i64 %16, %18
  store i64 %19, ptr %5, align 8
  %20 = load i64, ptr %5, align 8
  %21 = load ptr, ptr %3, align 8
  %22 = getelementptr inbounds %struct.CPU, ptr %21, i32 0, i32 0
  %23 = load i64, ptr %4, align 8
  %24 = lshr i64 %23, 2
  %25 = and i64 %24, 31
  %26 = getelementptr inbounds [32 x i64], ptr %22, i64 0, i64 %25
  store i64 %20, ptr %26, align 8
  %27 = load ptr, ptr %3, align 8
  %28 = load i64, ptr %4, align 8
  %29 = add i64 %28, 4
  %30 = call i64 @F151(ptr noundef %27, i64 noundef %29)
  %31 = load i64, ptr %5, align 8
  %32 = add i64 %30, %31
  ret i64 %32
}

; Function Attrs: nounwind
define dso_local i64 @F153(ptr noundef %0, i64 noundef %1) #0 {
  %3 = alloca ptr, align 8
  %4 = alloca i64, align 8
  %5 = alloca i64, align 8
  store ptr %0, ptr %3, align 8
  store i64 %1, ptr %4, align 8
  %6 = load ptr, ptr %3, align 8
  %7 = getelementptr inbounds %struct.CPU, ptr %6, i32 0, i32 0
  %8 = load i64, ptr %4, align 8
  %9 = lshr i64 %8, 2
  %10 = and i64 %9, 31
  %11 = getelementptr inbounds [32 x i64], ptr %7, i64 0, i64 %10
  %12 = load i64, ptr %11, align 8
  %13 = load i64, ptr %4, align 8
  %14 = add i64 %12, %13
  store i64 %14, ptr %5, align 8
  %15 = load i64, ptr %5, align 8
  %16 = mul i64 %15, 37
  %17 = load i64, ptr %5, align 8
  %18 = lshr i64 %17, 7
  %19 = xor i64 %16, %18
  store i64 %19, ptr %5, align 8
  %20 = load i64, ptr %5, align 8
  %21 = load ptr, ptr %3, align 8
  %22 = getelementptr inbounds %struct.CPU, ptr %21, i32 0, i32 0
  %23 = load i64, ptr %4, align 8
  %24 = lshr i64 %23, 2
  %25 = and i64 %24, 31
  %26 = getelementptr inbounds [32 x i64], ptr %22, i64 0, i64 %25
  store i64 %20, ptr %26, align 8
  %27 = load ptr, ptr %3, align 8
  %28 = load i64, ptr %4, align 8
  %29 = add i64 %28, 4
  %30 = call i64 @F152(ptr noundef %27, i64 noundef %29)
  %31 = load i64, ptr %5, align 8
  %32 = add i64 %30, %31
  ret i64 %32
}

; Function Attrs: nounwind
define dso_local i64 @F154(ptr noundef %0, i64 noundef %1) #0 {
  %3 = alloca ptr, align 8
  %4 = alloca i64, align 8
  %5 = alloca i64, align 8
  store ptr %0, ptr %3, align 8
  store i64 %1, ptr %4, align 8
  %6 = load ptr, ptr %3, align 8
  %7 = getelementptr inbounds %struct.CPU, ptr %6, i32 0, i32 0
  %8 = load i64, ptr %4, align 8
  %9 = lshr i64 %8, 2
  %10 = and i64 %9, 31
  %11 = getelementptr inbounds [32 x i64], ptr %7, i64 0, i64 %10
  %12 = load i64, ptr %11, align 8
  %13 = load i64, ptr %4, align 8
  %14 = add i64 %12, %13
  store i64 %14, ptr %5, align 8
  %15 = load i64, ptr %5, align 8
  %16 = mul i64 %15, 37
  %17 = load i64, ptr %5, align 8
  %18 = lshr i64 %17, 7
  %19 = xor i64 %16, %18
  store i64 %19, ptr %5, align 8
  %20 = load i64, ptr %5, align 8
  %21 = load ptr, ptr %3, align 8
  %22 = getelementptr inbounds %struct.CPU, ptr %21, i32 0, i32 0
  %23 = load i64, ptr %4, align 8
  %24 = lshr i64 %23, 2
  %25 = and i64 %24, 31
  %26 = getelementptr inbounds [32 x i64], ptr %22, i64 0, i64 %25
  store i64 %20, ptr %26, align 8
  %27 = load ptr, ptr %3, align 8
  %28 = load i64, ptr %4, align 8
  %29 = add i64 %28, 4
  %30 = call i64 @F153(ptr noundef %27, i64 noundef %29)
  %31 = load i64, ptr %5, align 8
  %32 = add i64 %30, %31
  ret i64 %32
}

; Function Attrs: nounwind
define dso_local i64 @F155(ptr noundef %0, i64 noundef %1) #0 {
  %3 = alloca ptr, align 8
  %4 = alloca i64, align 8
  %5 = alloca i64, align 8
  store ptr %0, ptr %3, align 8
  store i64 %1, ptr %4, align 8
  %6 = load ptr, ptr %3, align 8
  %7 = getelementptr inbounds %struct.CPU, ptr %6, i32 0, i32 0
  %8 = load i64, ptr %4, align 8
  %9 = lshr i64 %8, 2
  %10 = and i64 %9, 31
  %11 = getelementptr inbounds [32 x i64], ptr %7, i64 0, i64 %10
  %12 = load i64, ptr %11, align 8
  %13 = load i64, ptr %4, align 8
  %14 = add i64 %12, %13
  store i64 %14, ptr %5, align 8
  %15 = load i64, ptr %5, align 8
  %16 = mul i64 %15, 37
  %17 = load i64, ptr %5, align 8
  %18 = lshr i64 %17, 7
  %19 = xor i64 %16, %18
  store i64 %19, ptr %5, align 8
  %20 = load i64, ptr %5, align 8
  %21 = load ptr, ptr %3, align 8
  %22 = getelementptr inbounds %struct.CPU, ptr %21, i32 0, i32 0
  %23 = load i64, ptr %4, align 8
  %24 = lshr i64 %23, 2
  %25 = and i64 %24, 31
  %26 = getelementptr inbounds [32 x i64], ptr %22, i64 0, i64 %25
  store i64 %20, ptr %26, align 8
  %27 = load ptr, ptr %3, align 8
  %28 = load i64, ptr %4, align 8
  %29 = add i64 %28, 4
  %30 = call i64 @F154(ptr noundef %27, i64 noundef %29)
  %31 = load i64, ptr %5, align 8
  %32 = add i64 %30, %31
  ret i64 %32
}

; Function Attrs: nounwind
define dso_local i64 @F156(ptr noundef %0, i64 noundef %1) #0 {
  %3 = alloca ptr, align 8
  %4 = alloca i64, align 8
  %5 = alloca i64, align 8
  store ptr %0, ptr %3, align 8
  store i64 %1, ptr %4, align 8
  %6 = load ptr, ptr %3, align 8
  %7 = getelementptr inbounds %struct.CPU, ptr %6, i32 0, i32 0
  %8 = load i64, ptr %4, align 8
  %9 = lshr i64 %8, 2
  %10 = and i64 %9, 31
  %11 = getelementptr inbounds [32 x i64], ptr %7, i64 0, i64 %10
  %12 = load i64, ptr %11, align 8
  %13 = load i64, ptr %4, align 8
  %14 = add i64 %12, %13
  store i64 %14, ptr %5, align 8
  %15 = load i64, ptr %5, align 8
  %16 = mul i64 %15, 37
  %17 = load i64, ptr %5, align 8
  %18 = lshr i64 %17, 7
  %19 = xor i64 %16, %18
  store i64 %19, ptr %5, align 8
  %20 = load i64, ptr %5, align 8
  %21 = load ptr, ptr %3, align 8
  %22 = getelementptr inbounds %struct.CPU, ptr %21, i32 0, i32 0
  %23 = load i64, ptr %4, align 8
  %24 = lshr i64 %23, 2
  %25 = and i64 %24, 31
  %26 = getelementptr inbounds [32 x i64], ptr %22, i64 0, i64 %25
  store i64 %20, ptr %26, align 8
  %27 = load ptr, ptr %3, align 8
  %28 = load i64, ptr %4, align 8
  %29 = add i64 %28, 4
  %30 = call i64 @F155(ptr noundef %27, i64 noundef %29)
  %31 = load i64, ptr %5, align 8
  %32 = add i64 %30, %31
  ret i64 %32
}

; Function Attrs: nounwind
define dso_local i64 @F157(ptr noundef %0, i64 noundef %1) #0 {
  %3 = alloca ptr, align 8
  %4 = alloca i64, align 8
  %5 = alloca i64, align 8
  store ptr %0, ptr %3, align 8
  store i64 %1, ptr %4, align 8
  %6 = load ptr, ptr %3, align 8
  %7 = getelementptr inbounds %struct.CPU, ptr %6, i32 0, i32 0
  %8 = load i64, ptr %4, align 8
  %9 = lshr i64 %8, 2
  %10 = and i64 %9, 31
  %11 = getelementptr inbounds [32 x i64], ptr %7, i64 0, i64 %10
  %12 = load i64, ptr %11, align 8
  %13 = load i64, ptr %4, align 8
  %14 = add i64 %12, %13
  store i64 %14, ptr %5, align 8
  %15 = load i64, ptr %5, align 8
  %16 = mul i64 %15, 37
  %17 = load i64, ptr %5, align 8
  %18 = lshr i64 %17, 7
  %19 = xor i64 %16, %18
  store i64 %19, ptr %5, align 8
  %20 = load i64, ptr %5, align 8
  %21 = load ptr, ptr %3, align 8
  %22 = getelementptr inbounds %struct.CPU, ptr %21, i32 0, i32 0
  %23 = load i64, ptr %4, align 8
  %24 = lshr i64 %23, 2
  %25 = and i64 %24, 31
  %26 = getelementptr inbounds [32 x i64], ptr %22, i64 0, i64 %25
  store i64 %20, ptr %26, align 8
  %27 = load ptr, ptr %3, align 8
  %28 = load i64, ptr %4, align 8
  %29 = add i64 %28, 4
  %30 = call i64 @F156(ptr noundef %27, i64 noundef %29)
  %31 = load i64, ptr %5, align 8
  %32 = add i64 %30, %31
  ret i64 %32
}

; Function Attrs: nounwind
define dso_local i64 @F158(ptr noundef %0, i64 noundef %1) #0 {
  %3 = alloca ptr, align 8
  %4 = alloca i64, align 8
  %5 = alloca i64, align 8
  store ptr %0, ptr %3, align 8
  store i64 %1, ptr %4, align 8
  %6 = load ptr, ptr %3, align 8
  %7 = getelementptr inbounds %struct.CPU, ptr %6, i32 0, i32 0
  %8 = load i64, ptr %4, align 8
  %9 = lshr i64 %8, 2
  %10 = and i64 %9, 31
  %11 = getelementptr inbounds [32 x i64], ptr %7, i64 0, i64 %10
  %12 = load i64, ptr %11, align 8
  %13 = load i64, ptr %4, align 8
  %14 = add i64 %12, %13
  store i64 %14, ptr %5, align 8
  %15 = load i64, ptr %5, align 8
  %16 = mul i64 %15, 37
  %17 = load i64, ptr %5, align 8
  %18 = lshr i64 %17, 7
  %19 = xor i64 %16, %18
  store i64 %19, ptr %5, align 8
  %20 = load i64, ptr %5, align 8
  %21 = load ptr, ptr %3, align 8
  %22 = getelementptr inbounds %struct.CPU, ptr %21, i32 0, i32 0
  %23 = load i64, ptr %4, align 8
  %24 = lshr i64 %23, 2
  %25 = and i64 %24, 31
  %26 = getelementptr inbounds [32 x i64], ptr %22, i64 0, i64 %25
  store i64 %20, ptr %26, align 8
  %27 = load ptr, ptr %3, align 8
  %28 = load i64, ptr %4, align 8
  %29 = add i64 %28, 4
  %30 = call i64 @F157(ptr noundef %27, i64 noundef %29)
  %31 = load i64, ptr %5, align 8
  %32 = add i64 %30, %31
  ret i64 %32
}

; Function Attrs: nounwind
define dso_local i64 @F159(ptr noundef %0, i64 noundef %1) #0 {
  %3 = alloca ptr, align 8
  %4 = alloca i64, align 8
  %5 = alloca i64, align 8
  store ptr %0, ptr %3, align 8
  store i64 %1, ptr %4, align 8
  %6 = load ptr, ptr %3, align 8
  %7 = getelementptr inbounds %struct.CPU, ptr %6, i32 0, i32 0
  %8 = load i64, ptr %4, align 8
  %9 = lshr i64 %8, 2
  %10 = and i64 %9, 31
  %11 = getelementptr inbounds [32 x i64], ptr %7, i64 0, i64 %10
  %12 = load i64, ptr %11, align 8
  %13 = load i64, ptr %4, align 8
  %14 = add i64 %12, %13
  store i64 %14, ptr %5, align 8
  %15 = load i64, ptr %5, align 8
  %16 = mul i64 %15, 37
  %17 = load i64, ptr %5, align 8
  %18 = lshr i64 %17, 7
  %19 = xor i64 %16, %18
  store i64 %19, ptr %5, align 8
  %20 = load i64, ptr %5, align 8
  %21 = load ptr, ptr %3, align 8
  %22 = getelementptr inbounds %struct.CPU, ptr %21, i32 0, i32 0
  %23 = load i64, ptr %4, align 8
  %24 = lshr i64 %23, 2
  %25 = and i64 %24, 31
  %26 = getelementptr inbounds [32 x i64], ptr %22, i64 0, i64 %25
  store i64 %20, ptr %26, align 8
  %27 = load ptr, ptr %3, align 8
  %28 = load i64, ptr %4, align 8
  %29 = add i64 %28, 4
  %30 = call i64 @F158(ptr noundef %27, i64 noundef %29)
  %31 = load i64, ptr %5, align 8
  %32 = add i64 %30, %31
  ret i64 %32
}

; Function Attrs: nounwind
define dso_local i64 @F160(ptr noundef %0, i64 noundef %1) #0 {
  %3 = alloca ptr, align 8
  %4 = alloca i64, align 8
  %5 = alloca i64, align 8
  store ptr %0, ptr %3, align 8
  store i64 %1, ptr %4, align 8
  %6 = load ptr, ptr %3, align 8
  %7 = getelementptr inbounds %struct.CPU, ptr %6, i32 0, i32 0
  %8 = load i64, ptr %4, align 8
  %9 = lshr i64 %8, 2
  %10 = and i64 %9, 31
  %11 = getelementptr inbounds [32 x i64], ptr %7, i64 0, i64 %10
  %12 = load i64, ptr %11, align 8
  %13 = load i64, ptr %4, align 8
  %14 = add i64 %12, %13
  store i64 %14, ptr %5, align 8
  %15 = load i64, ptr %5, align 8
  %16 = mul i64 %15, 37
  %17 = load i64, ptr %5, align 8
  %18 = lshr i64 %17, 7
  %19 = xor i64 %16, %18
  store i64 %19, ptr %5, align 8
  %20 = load i64, ptr %5, align 8
  %21 = load ptr, ptr %3, align 8
  %22 = getelementptr inbounds %struct.CPU, ptr %21, i32 0, i32 0
  %23 = load i64, ptr %4, align 8
  %24 = lshr i64 %23, 2
  %25 = and i64 %24, 31
  %26 = getelementptr inbounds [32 x i64], ptr %22, i64 0, i64 %25
  store i64 %20, ptr %26, align 8
  %27 = load ptr, ptr %3, align 8
  %28 = load i64, ptr %4, align 8
  %29 = add i64 %28, 4
  %30 = call i64 @F159(ptr noundef %27, i64 noundef %29)
  %31 = load i64, ptr %5, align 8
  %32 = add i64 %30, %31
  ret i64 %32
}

; Function Attrs: nounwind
define dso_local i64 @F161(ptr noundef %0, i64 noundef %1) #0 {
  %3 = alloca ptr, align 8
  %4 = alloca i64, align 8
  %5 = alloca i64, align 8
  store ptr %0, ptr %3, align 8
  store i64 %1, ptr %4, align 8
  %6 = load ptr, ptr %3, align 8
  %7 = getelementptr inbounds %struct.CPU, ptr %6, i32 0, i32 0
  %8 = load i64, ptr %4, align 8
  %9 = lshr i64 %8, 2
  %10 = and i64 %9, 31
  %11 = getelementptr inbounds [32 x i64], ptr %7, i64 0, i64 %10
  %12 = load i64, ptr %11, align 8
  %13 = load i64, ptr %4, align 8
  %14 = add i64 %12, %13
  store i64 %14, ptr %5, align 8
  %15 = load i64, ptr %5, align 8
  %16 = mul i64 %15, 37
  %17 = load i64, ptr %5, align 8
  %18 = lshr i64 %17, 7
  %19 = xor i64 %16, %18
  store i64 %19, ptr %5, align 8
  %20 = load i64, ptr %5, align 8
  %21 = load ptr, ptr %3, align 8
  %22 = getelementptr inbounds %struct.CPU, ptr %21, i32 0, i32 0
  %23 = load i64, ptr %4, align 8
  %24 = lshr i64 %23, 2
  %25 = and i64 %24, 31
  %26 = getelementptr inbounds [32 x i64], ptr %22, i64 0, i64 %25
  store i64 %20, ptr %26, align 8
  %27 = load ptr, ptr %3, align 8
  %28 = load i64, ptr %4, align 8
  %29 = add i64 %28, 4
  %30 = call i64 @F160(ptr noundef %27, i64 noundef %29)
  %31 = load i64, ptr %5, align 8
  %32 = add i64 %30, %31
  ret i64 %32
}

; Function Attrs: nounwind
define dso_local i64 @F162(ptr noundef %0, i64 noundef %1) #0 {
  %3 = alloca ptr, align 8
  %4 = alloca i64, align 8
  %5 = alloca i64, align 8
  store ptr %0, ptr %3, align 8
  store i64 %1, ptr %4, align 8
  %6 = load ptr, ptr %3, align 8
  %7 = getelementptr inbounds %struct.CPU, ptr %6, i32 0, i32 0
  %8 = load i64, ptr %4, align 8
  %9 = lshr i64 %8, 2
  %10 = and i64 %9, 31
  %11 = getelementptr inbounds [32 x i64], ptr %7, i64 0, i64 %10
  %12 = load i64, ptr %11, align 8
  %13 = load i64, ptr %4, align 8
  %14 = add i64 %12, %13
  store i64 %14, ptr %5, align 8
  %15 = load i64, ptr %5, align 8
  %16 = mul i64 %15, 37
  %17 = load i64, ptr %5, align 8
  %18 = lshr i64 %17, 7
  %19 = xor i64 %16, %18
  store i64 %19, ptr %5, align 8
  %20 = load i64, ptr %5, align 8
  %21 = load ptr, ptr %3, align 8
  %22 = getelementptr inbounds %struct.CPU, ptr %21, i32 0, i32 0
  %23 = load i64, ptr %4, align 8
  %24 = lshr i64 %23, 2
  %25 = and i64 %24, 31
  %26 = getelementptr inbounds [32 x i64], ptr %22, i64 0, i64 %25
  store i64 %20, ptr %26, align 8
  %27 = load ptr, ptr %3, align 8
  %28 = load i64, ptr %4, align 8
  %29 = add i64 %28, 4
  %30 = call i64 @F161(ptr noundef %27, i64 noundef %29)
  %31 = load i64, ptr %5, align 8
  %32 = add i64 %30, %31
  ret i64 %32
}

; Function Attrs: nounwind
define dso_local i64 @F163(ptr noundef %0, i64 noundef %1) #0 {
  %3 = alloca ptr, align 8
  %4 = alloca i64, align 8
  %5 = alloca i64, align 8
  store ptr %0, ptr %3, align 8
  store i64 %1, ptr %4, align 8
  %6 = load ptr, ptr %3, align 8
  %7 = getelementptr inbounds %struct.CPU, ptr %6, i32 0, i32 0
  %8 = load i64, ptr %4, align 8
  %9 = lshr i64 %8, 2
  %10 = and i64 %9, 31
  %11 = getelementptr inbounds [32 x i64], ptr %7, i64 0, i64 %10
  %12 = load i64, ptr %11, align 8
  %13 = load i64, ptr %4, align 8
  %14 = add i64 %12, %13
  store i64 %14, ptr %5, align 8
  %15 = load i64, ptr %5, align 8
  %16 = mul i64 %15, 37
  %17 = load i64, ptr %5, align 8
  %18 = lshr i64 %17, 7
  %19 = xor i64 %16, %18
  store i64 %19, ptr %5, align 8
  %20 = load i64, ptr %5, align 8
  %21 = load ptr, ptr %3, align 8
  %22 = getelementptr inbounds %struct.CPU, ptr %21, i32 0, i32 0
  %23 = load i64, ptr %4, align 8
  %24 = lshr i64 %23, 2
  %25 = and i64 %24, 31
  %26 = getelementptr inbounds [32 x i64], ptr %22, i64 0, i64 %25
  store i64 %20, ptr %26, align 8
  %27 = load ptr, ptr %3, align 8
  %28 = load i64, ptr %4, align 8
  %29 = add i64 %28, 4
  %30 = call i64 @F162(ptr noundef %27, i64 noundef %29)
  %31 = load i64, ptr %5, align 8
  %32 = add i64 %30, %31
  ret i64 %32
}

; Function Attrs: nounwind
define dso_local i64 @F164(ptr noundef %0, i64 noundef %1) #0 {
  %3 = alloca ptr, align 8
  %4 = alloca i64, align 8
  %5 = alloca i64, align 8
  store ptr %0, ptr %3, align 8
  store i64 %1, ptr %4, align 8
  %6 = load ptr, ptr %3, align 8
  %7 = getelementptr inbounds %struct.CPU, ptr %6, i32 0, i32 0
  %8 = load i64, ptr %4, align 8
  %9 = lshr i64 %8, 2
  %10 = and i64 %9, 31
  %11 = getelementptr inbounds [32 x i64], ptr %7, i64 0, i64 %10
  %12 = load i64, ptr %11, align 8
  %13 = load i64, ptr %4, align 8
  %14 = add i64 %12, %13
  store i64 %14, ptr %5, align 8
  %15 = load i64, ptr %5, align 8
  %16 = mul i64 %15, 37
  %17 = load i64, ptr %5, align 8
  %18 = lshr i64 %17, 7
  %19 = xor i64 %16, %18
  store i64 %19, ptr %5, align 8
  %20 = load i64, ptr %5, align 8
  %21 = load ptr, ptr %3, align 8
  %22 = getelementptr inbounds %struct.CPU, ptr %21, i32 0, i32 0
  %23 = load i64, ptr %4, align 8
  %24 = lshr i64 %23, 2
  %25 = and i64 %24, 31
  %26 = getelementptr inbounds [32 x i64], ptr %22, i64 0, i64 %25
  store i64 %20, ptr %26, align 8
  %27 = load ptr, ptr %3, align 8
  %28 = load i64, ptr %4, align 8
  %29 = add i64 %28, 4
  %30 = call i64 @F163(ptr noundef %27, i64 noundef %29)
  %31 = load i64, ptr %5, align 8
  %32 = add i64 %30, %31
  ret i64 %32
}

; Function Attrs: nounwind
define dso_local i64 @F165(ptr noundef %0, i64 noundef %1) #0 {
  %3 = alloca ptr, align 8
  %4 = alloca i64, align 8
  %5 = alloca i64, align 8
  store ptr %0, ptr %3, align 8
  store i64 %1, ptr %4, align 8
  %6 = load ptr, ptr %3, align 8
  %7 = getelementptr inbounds %struct.CPU, ptr %6, i32 0, i32 0
  %8 = load i64, ptr %4, align 8
  %9 = lshr i64 %8, 2
  %10 = and i64 %9, 31
  %11 = getelementptr inbounds [32 x i64], ptr %7, i64 0, i64 %10
  %12 = load i64, ptr %11, align 8
  %13 = load i64, ptr %4, align 8
  %14 = add i64 %12, %13
  store i64 %14, ptr %5, align 8
  %15 = load i64, ptr %5, align 8
  %16 = mul i64 %15, 37
  %17 = load i64, ptr %5, align 8
  %18 = lshr i64 %17, 7
  %19 = xor i64 %16, %18
  store i64 %19, ptr %5, align 8
  %20 = load i64, ptr %5, align 8
  %21 = load ptr, ptr %3, align 8
  %22 = getelementptr inbounds %struct.CPU, ptr %21, i32 0, i32 0
  %23 = load i64, ptr %4, align 8
  %24 = lshr i64 %23, 2
  %25 = and i64 %24, 31
  %26 = getelementptr inbounds [32 x i64], ptr %22, i64 0, i64 %25
  store i64 %20, ptr %26, align 8
  %27 = load ptr, ptr %3, align 8
  %28 = load i64, ptr %4, align 8
  %29 = add i64 %28, 4
  %30 = call i64 @F164(ptr noundef %27, i64 noundef %29)
  %31 = load i64, ptr %5, align 8
  %32 = add i64 %30, %31
  ret i64 %32
}

; Function Attrs: nounwind
define dso_local i64 @F166(ptr noundef %0, i64 noundef %1) #0 {
  %3 = alloca ptr, align 8
  %4 = alloca i64, align 8
  %5 = alloca i64, align 8
  store ptr %0, ptr %3, align 8
  store i64 %1, ptr %4, align 8
  %6 = load ptr, ptr %3, align 8
  %7 = getelementptr inbounds %struct.CPU, ptr %6, i32 0, i32 0
  %8 = load i64, ptr %4, align 8
  %9 = lshr i64 %8, 2
  %10 = and i64 %9, 31
  %11 = getelementptr inbounds [32 x i64], ptr %7, i64 0, i64 %10
  %12 = load i64, ptr %11, align 8
  %13 = load i64, ptr %4, align 8
  %14 = add i64 %12, %13
  store i64 %14, ptr %5, align 8
  %15 = load i64, ptr %5, align 8
  %16 = mul i64 %15, 37
  %17 = load i64, ptr %5, align 8
  %18 = lshr i64 %17, 7
  %19 = xor i64 %16, %18
  store i64 %19, ptr %5, align 8
  %20 = load i64, ptr %5, align 8
  %21 = load ptr, ptr %3, align 8
  %22 = getelementptr inbounds %struct.CPU, ptr %21, i32 0, i32 0
  %23 = load i64, ptr %4, align 8
  %24 = lshr i64 %23, 2
  %25 = and i64 %24, 31
  %26 = getelementptr inbounds [32 x i64], ptr %22, i64 0, i64 %25
  store i64 %20, ptr %26, align 8
  %27 = load ptr, ptr %3, align 8
  %28 = load i64, ptr %4, align 8
  %29 = add i64 %28, 4
  %30 = call i64 @F165(ptr noundef %27, i64 noundef %29)
  %31 = load i64, ptr %5, align 8
  %32 = add i64 %30, %31
  ret i64 %32
}

; Function Attrs: nounwind
define dso_local i64 @F167(ptr noundef %0, i64 noundef %1) #0 {
  %3 = alloca ptr, align 8
  %4 = alloca i64, align 8
  %5 = alloca i64, align 8
  store ptr %0, ptr %3, align 8
  store i64 %1, ptr %4, align 8
  %6 = load ptr, ptr %3, align 8
  %7 = getelementptr inbounds %struct.CPU, ptr %6, i32 0, i32 0
  %8 = load i64, ptr %4, align 8
  %9 = lshr i64 %8, 2
  %10 = and i64 %9, 31
  %11 = getelementptr inbounds [32 x i64], ptr %7, i64 0, i64 %10
  %12 = load i64, ptr %11, align 8
  %13 = load i64, ptr %4, align 8
  %14 = add i64 %12, %13
  store i64 %14, ptr %5, align 8
  %15 = load i64, ptr %5, align 8
  %16 = mul i64 %15, 37
  %17 = load i64, ptr %5, align 8
  %18 = lshr i64 %17, 7
  %19 = xor i64 %16, %18
  store i64 %19, ptr %5, align 8
  %20 = load i64, ptr %5, align 8
  %21 = load ptr, ptr %3, align 8
  %22 = getelementptr inbounds %struct.CPU, ptr %21, i32 0, i32 0
  %23 = load i64, ptr %4, align 8
  %24 = lshr i64 %23, 2
  %25 = and i64 %24, 31
  %26 = getelementptr inbounds [32 x i64], ptr %22, i64 0, i64 %25
  store i64 %20, ptr %26, align 8
  %27 = load ptr, ptr %3, align 8
  %28 = load i64, ptr %4, align 8
  %29 = add i64 %28, 4
  %30 = call i64 @F166(ptr noundef %27, i64 noundef %29)
  %31 = load i64, ptr %5, align 8
  %32 = add i64 %30, %31
  ret i64 %32
}

; Function Attrs: nounwind
define dso_local i64 @F168(ptr noundef %0, i64 noundef %1) #0 {
  %3 = alloca ptr, align 8
  %4 = alloca i64, align 8
  %5 = alloca i64, align 8
  store ptr %0, ptr %3, align 8
  store i64 %1, ptr %4, align 8
  %6 = load ptr, ptr %3, align 8
  %7 = getelementptr inbounds %struct.CPU, ptr %6, i32 0, i32 0
  %8 = load i64, ptr %4, align 8
  %9 = lshr i64 %8, 2
  %10 = and i64 %9, 31
  %11 = getelementptr inbounds [32 x i64], ptr %7, i64 0, i64 %10
  %12 = load i64, ptr %11, align 8
  %13 = load i64, ptr %4, align 8
  %14 = add i64 %12, %13
  store i64 %14, ptr %5, align 8
  %15 = load i64, ptr %5, align 8
  %16 = mul i64 %15, 37
  %17 = load i64, ptr %5, align 8
  %18 = lshr i64 %17, 7
  %19 = xor i64 %16, %18
  store i64 %19, ptr %5, align 8
  %20 = load i64, ptr %5, align 8
  %21 = load ptr, ptr %3, align 8
  %22 = getelementptr inbounds %struct.CPU, ptr %21, i32 0, i32 0
  %23 = load i64, ptr %4, align 8
  %24 = lshr i64 %23, 2
  %25 = and i64 %24, 31
  %26 = getelementptr inbounds [32 x i64], ptr %22, i64 0, i64 %25
  store i64 %20, ptr %26, align 8
  %27 = load ptr, ptr %3, align 8
  %28 = load i64, ptr %4, align 8
  %29 = add i64 %28, 4
  %30 = call i64 @F167(ptr noundef %27, i64 noundef %29)
  %31 = load i64, ptr %5, align 8
  %32 = add i64 %30, %31
  ret i64 %32
}

; Function Attrs: nounwind
define dso_local i64 @F169(ptr noundef %0, i64 noundef %1) #0 {
  %3 = alloca ptr, align 8
  %4 = alloca i64, align 8
  %5 = alloca i64, align 8
  store ptr %0, ptr %3, align 8
  store i64 %1, ptr %4, align 8
  %6 = load ptr, ptr %3, align 8
  %7 = getelementptr inbounds %struct.CPU, ptr %6, i32 0, i32 0
  %8 = load i64, ptr %4, align 8
  %9 = lshr i64 %8, 2
  %10 = and i64 %9, 31
  %11 = getelementptr inbounds [32 x i64], ptr %7, i64 0, i64 %10
  %12 = load i64, ptr %11, align 8
  %13 = load i64, ptr %4, align 8
  %14 = add i64 %12, %13
  store i64 %14, ptr %5, align 8
  %15 = load i64, ptr %5, align 8
  %16 = mul i64 %15, 37
  %17 = load i64, ptr %5, align 8
  %18 = lshr i64 %17, 7
  %19 = xor i64 %16, %18
  store i64 %19, ptr %5, align 8
  %20 = load i64, ptr %5, align 8
  %21 = load ptr, ptr %3, align 8
  %22 = getelementptr inbounds %struct.CPU, ptr %21, i32 0, i32 0
  %23 = load i64, ptr %4, align 8
  %24 = lshr i64 %23, 2
  %25 = and i64 %24, 31
  %26 = getelementptr inbounds [32 x i64], ptr %22, i64 0, i64 %25
  store i64 %20, ptr %26, align 8
  %27 = load ptr, ptr %3, align 8
  %28 = load i64, ptr %4, align 8
  %29 = add i64 %28, 4
  %30 = call i64 @F168(ptr noundef %27, i64 noundef %29)
  %31 = load i64, ptr %5, align 8
  %32 = add i64 %30, %31
  ret i64 %32
}

; Function Attrs: nounwind
define dso_local i64 @F170(ptr noundef %0, i64 noundef %1) #0 {
  %3 = alloca ptr, align 8
  %4 = alloca i64, align 8
  %5 = alloca i64, align 8
  store ptr %0, ptr %3, align 8
  store i64 %1, ptr %4, align 8
  %6 = load ptr, ptr %3, align 8
  %7 = getelementptr inbounds %struct.CPU, ptr %6, i32 0, i32 0
  %8 = load i64, ptr %4, align 8
  %9 = lshr i64 %8, 2
  %10 = and i64 %9, 31
  %11 = getelementptr inbounds [32 x i64], ptr %7, i64 0, i64 %10
  %12 = load i64, ptr %11, align 8
  %13 = load i64, ptr %4, align 8
  %14 = add i64 %12, %13
  store i64 %14, ptr %5, align 8
  %15 = load i64, ptr %5, align 8
  %16 = mul i64 %15, 37
  %17 = load i64, ptr %5, align 8
  %18 = lshr i64 %17, 7
  %19 = xor i64 %16, %18
  store i64 %19, ptr %5, align 8
  %20 = load i64, ptr %5, align 8
  %21 = load ptr, ptr %3, align 8
  %22 = getelementptr inbounds %struct.CPU, ptr %21, i32 0, i32 0
  %23 = load i64, ptr %4, align 8
  %24 = lshr i64 %23, 2
  %25 = and i64 %24, 31
  %26 = getelementptr inbounds [32 x i64], ptr %22, i64 0, i64 %25
  store i64 %20, ptr %26, align 8
  %27 = load ptr, ptr %3, align 8
  %28 = load i64, ptr %4, align 8
  %29 = add i64 %28, 4
  %30 = call i64 @F169(ptr noundef %27, i64 noundef %29)
  %31 = load i64, ptr %5, align 8
  %32 = add i64 %30, %31
  ret i64 %32
}

; Function Attrs: nounwind
define dso_local i64 @F171(ptr noundef %0, i64 noundef %1) #0 {
  %3 = alloca ptr, align 8
  %4 = alloca i64, align 8
  %5 = alloca i64, align 8
  store ptr %0, ptr %3, align 8
  store i64 %1, ptr %4, align 8
  %6 = load ptr, ptr %3, align 8
  %7 = getelementptr inbounds %struct.CPU, ptr %6, i32 0, i32 0
  %8 = load i64, ptr %4, align 8
  %9 = lshr i64 %8, 2
  %10 = and i64 %9, 31
  %11 = getelementptr inbounds [32 x i64], ptr %7, i64 0, i64 %10
  %12 = load i64, ptr %11, align 8
  %13 = load i64, ptr %4, align 8
  %14 = add i64 %12, %13
  store i64 %14, ptr %5, align 8
  %15 = load i64, ptr %5, align 8
  %16 = mul i64 %15, 37
  %17 = load i64, ptr %5, align 8
  %18 = lshr i64 %17, 7
  %19 = xor i64 %16, %18
  store i64 %19, ptr %5, align 8
  %20 = load i64, ptr %5, align 8
  %21 = load ptr, ptr %3, align 8
  %22 = getelementptr inbounds %struct.CPU, ptr %21, i32 0, i32 0
  %23 = load i64, ptr %4, align 8
  %24 = lshr i64 %23, 2
  %25 = and i64 %24, 31
  %26 = getelementptr inbounds [32 x i64], ptr %22, i64 0, i64 %25
  store i64 %20, ptr %26, align 8
  %27 = load ptr, ptr %3, align 8
  %28 = load i64, ptr %4, align 8
  %29 = add i64 %28, 4
  %30 = call i64 @F170(ptr noundef %27, i64 noundef %29)
  %31 = load i64, ptr %5, align 8
  %32 = add i64 %30, %31
  ret i64 %32
}

; Function Attrs: nounwind
define dso_local i64 @F172(ptr noundef %0, i64 noundef %1) #0 {
  %3 = alloca ptr, align 8
  %4 = alloca i64, align 8
  %5 = alloca i64, align 8
  store ptr %0, ptr %3, align 8
  store i64 %1, ptr %4, align 8
  %6 = load ptr, ptr %3, align 8
  %7 = getelementptr inbounds %struct.CPU, ptr %6, i32 0, i32 0
  %8 = load i64, ptr %4, align 8
  %9 = lshr i64 %8, 2
  %10 = and i64 %9, 31
  %11 = getelementptr inbounds [32 x i64], ptr %7, i64 0, i64 %10
  %12 = load i64, ptr %11, align 8
  %13 = load i64, ptr %4, align 8
  %14 = add i64 %12, %13
  store i64 %14, ptr %5, align 8
  %15 = load i64, ptr %5, align 8
  %16 = mul i64 %15, 37
  %17 = load i64, ptr %5, align 8
  %18 = lshr i64 %17, 7
  %19 = xor i64 %16, %18
  store i64 %19, ptr %5, align 8
  %20 = load i64, ptr %5, align 8
  %21 = load ptr, ptr %3, align 8
  %22 = getelementptr inbounds %struct.CPU, ptr %21, i32 0, i32 0
  %23 = load i64, ptr %4, align 8
  %24 = lshr i64 %23, 2
  %25 = and i64 %24, 31
  %26 = getelementptr inbounds [32 x i64], ptr %22, i64 0, i64 %25
  store i64 %20, ptr %26, align 8
  %27 = load ptr, ptr %3, align 8
  %28 = load i64, ptr %4, align 8
  %29 = add i64 %28, 4
  %30 = call i64 @F171(ptr noundef %27, i64 noundef %29)
  %31 = load i64, ptr %5, align 8
  %32 = add i64 %30, %31
  ret i64 %32
}

; Function Attrs: nounwind
define dso_local i64 @F173(ptr noundef %0, i64 noundef %1) #0 {
  %3 = alloca ptr, align 8
  %4 = alloca i64, align 8
  %5 = alloca i64, align 8
  store ptr %0, ptr %3, align 8
  store i64 %1, ptr %4, align 8
  %6 = load ptr, ptr %3, align 8
  %7 = getelementptr inbounds %struct.CPU, ptr %6, i32 0, i32 0
  %8 = load i64, ptr %4, align 8
  %9 = lshr i64 %8, 2
  %10 = and i64 %9, 31
  %11 = getelementptr inbounds [32 x i64], ptr %7, i64 0, i64 %10
  %12 = load i64, ptr %11, align 8
  %13 = load i64, ptr %4, align 8
  %14 = add i64 %12, %13
  store i64 %14, ptr %5, align 8
  %15 = load i64, ptr %5, align 8
  %16 = mul i64 %15, 37
  %17 = load i64, ptr %5, align 8
  %18 = lshr i64 %17, 7
  %19 = xor i64 %16, %18
  store i64 %19, ptr %5, align 8
  %20 = load i64, ptr %5, align 8
  %21 = load ptr, ptr %3, align 8
  %22 = getelementptr inbounds %struct.CPU, ptr %21, i32 0, i32 0
  %23 = load i64, ptr %4, align 8
  %24 = lshr i64 %23, 2
  %25 = and i64 %24, 31
  %26 = getelementptr inbounds [32 x i64], ptr %22, i64 0, i64 %25
  store i64 %20, ptr %26, align 8
  %27 = load ptr, ptr %3, align 8
  %28 = load i64, ptr %4, align 8
  %29 = add i64 %28, 4
  %30 = call i64 @F172(ptr noundef %27, i64 noundef %29)
  %31 = load i64, ptr %5, align 8
  %32 = add i64 %30, %31
  ret i64 %32
}

; Function Attrs: nounwind
define dso_local i64 @F174(ptr noundef %0, i64 noundef %1) #0 {
  %3 = alloca ptr, align 8
  %4 = alloca i64, align 8
  %5 = alloca i64, align 8
  store ptr %0, ptr %3, align 8
  store i64 %1, ptr %4, align 8
  %6 = load ptr, ptr %3, align 8
  %7 = getelementptr inbounds %struct.CPU, ptr %6, i32 0, i32 0
  %8 = load i64, ptr %4, align 8
  %9 = lshr i64 %8, 2
  %10 = and i64 %9, 31
  %11 = getelementptr inbounds [32 x i64], ptr %7, i64 0, i64 %10
  %12 = load i64, ptr %11, align 8
  %13 = load i64, ptr %4, align 8
  %14 = add i64 %12, %13
  store i64 %14, ptr %5, align 8
  %15 = load i64, ptr %5, align 8
  %16 = mul i64 %15, 37
  %17 = load i64, ptr %5, align 8
  %18 = lshr i64 %17, 7
  %19 = xor i64 %16, %18
  store i64 %19, ptr %5, align 8
  %20 = load i64, ptr %5, align 8
  %21 = load ptr, ptr %3, align 8
  %22 = getelementptr inbounds %struct.CPU, ptr %21, i32 0, i32 0
  %23 = load i64, ptr %4, align 8
  %24 = lshr i64 %23, 2
  %25 = and i64 %24, 31
  %26 = getelementptr inbounds [32 x i64], ptr %22, i64 0, i64 %25
  store i64 %20, ptr %26, align 8
  %27 = load ptr, ptr %3, align 8
  %28 = load i64, ptr %4, align 8
  %29 = add i64 %28, 4
  %30 = call i64 @F173(ptr noundef %27, i64 noundef %29)
  %31 = load i64, ptr %5, align 8
  %32 = add i64 %30, %31
  ret i64 %32
}

; Function Attrs: nounwind
define dso_local i64 @F175(ptr noundef %0, i64 noundef %1) #0 {
  %3 = alloca ptr, align 8
  %4 = alloca i64, align 8
  %5 = alloca i64, align 8
  store ptr %0, ptr %3, align 8
  store i64 %1, ptr %4, align 8
  %6 = load ptr, ptr %3, align 8
  %7 = getelementptr inbounds %struct.CPU, ptr %6, i32 0, i32 0
  %8 = load i64, ptr %4, align 8
  %9 = lshr i64 %8, 2
  %10 = and i64 %9, 31
  %11 = getelementptr inbounds [32 x i64], ptr %7, i64 0, i64 %10
  %12 = load i64, ptr %11, align 8
  %13 = load i64, ptr %4, align 8
  %14 = add i64 %12, %13
  store i64 %14, ptr %5, align 8
  %15 = load i64, ptr %5, align 8
  %16 = mul i64 %15, 37
  %17 = load i64, ptr %5, align 8
  %18 = lshr i64 %17, 7
  %19 = xor i64 %16, %18
  store i64 %19, ptr %5, align 8
  %20 = load i64, ptr %5, align 8
  %21 = load ptr, ptr %3, align 8
  %22 = getelementptr inbounds %struct.CPU, ptr %21, i32 0, i32 0
  %23 = load i64, ptr %4, align 8
  %24 = lshr i64 %23, 2
  %25 = and i64 %24, 31
  %26 = getelementptr inbounds [32 x i64], ptr %22, i64 0, i64 %25
  store i64 %20, ptr %26, align 8
  %27 = load ptr, ptr %3, align 8
  %28 = load i64, ptr %4, align 8
  %29 = add i64 %28, 4
  %30 = call i64 @F174(ptr noundef %27, i64 noundef %29)
  %31 = load i64, ptr %5, align 8
  %32 = add i64 %30, %31
  ret i64 %32
}

; Function Attrs: nounwind
define dso_local i64 @F176(ptr noundef %0, i64 noundef %1) #0 {
  %3 = alloca ptr, align 8
  %4 = alloca i64, align 8
  %5 = alloca i64, align 8
  store ptr %0, ptr %3, align 8
  store i64 %1, ptr %4, align 8
  %6 = load ptr, ptr %3, align 8
  %7 = getelementptr inbounds %struct.CPU, ptr %6, i32 0, i32 0
  %8 = load i64, ptr %4, align 8
  %9 = lshr i64 %8, 2
  %10 = and i64 %9, 31
  %11 = getelementptr inbounds [32 x i64], ptr %7, i64 0, i64 %10
  %12 = load i64, ptr %11, align 8
  %13 = load i64, ptr %4, align 8
  %14 = add i64 %12, %13
  store i64 %14, ptr %5, align 8
  %15 = load i64, ptr %5, align 8
  %16 = mul i64 %15, 37
  %17 = load i64, ptr %5, align 8
  %18 = lshr i64 %17, 7
  %19 = xor i64 %16, %18
  store i64 %19, ptr %5, align 8
  %20 = load i64, ptr %5, align 8
  %21 = load ptr, ptr %3, align 8
  %22 = getelementptr inbounds %struct.CPU, ptr %21, i32 0, i32 0
  %23 = load i64, ptr %4, align 8
  %24 = lshr i64 %23, 2
  %25 = and i64 %24, 31
  %26 = getelementptr inbounds [32 x i64], ptr %22, i64 0, i64 %25
  store i64 %20, ptr %26, align 8
  %27 = load ptr, ptr %3, align 8
  %28 = load i64, ptr %4, align 8
  %29 = add i64 %28, 4
  %30 = call i64 @F175(ptr noundef %27, i64 noundef %29)
  %31 = load i64, ptr %5, align 8
  %32 = add i64 %30, %31
  ret i64 %32
}

; Function Attrs: nounwind
define dso_local i64 @F177(ptr noundef %0, i64 noundef %1) #0 {
  %3 = alloca ptr, align 8
  %4 = alloca i64, align 8
  %5 = alloca i64, align 8
  store ptr %0, ptr %3, align 8
  store i64 %1, ptr %4, align 8
  %6 = load ptr, ptr %3, align 8
  %7 = getelementptr inbounds %struct.CPU, ptr %6, i32 0, i32 0
  %8 = load i64, ptr %4, align 8
  %9 = lshr i64 %8, 2
  %10 = and i64 %9, 31
  %11 = getelementptr inbounds [32 x i64], ptr %7, i64 0, i64 %10
  %12 = load i64, ptr %11, align 8
  %13 = load i64, ptr %4, align 8
  %14 = add i64 %12, %13
  store i64 %14, ptr %5, align 8
  %15 = load i64, ptr %5, align 8
  %16 = mul i64 %15, 37
  %17 = load i64, ptr %5, align 8
  %18 = lshr i64 %17, 7
  %19 = xor i64 %16, %18
  store i64 %19, ptr %5, align 8
  %20 = load i64, ptr %5, align 8
  %21 = load ptr, ptr %3, align 8
  %22 = getelementptr inbounds %struct.CPU, ptr %21, i32 0, i32 0
  %23 = load i64, ptr %4, align 8
  %24 = lshr i64 %23, 2
  %25 = and i64 %24, 31
  %26 = getelementptr inbounds [32 x i64], ptr %22, i64 0, i64 %25
  store i64 %20, ptr %26, align 8
  %27 = load ptr, ptr %3, align 8
  %28 = load i64, ptr %4, align 8
  %29 = add i64 %28, 4
  %30 = call i64 @F176(ptr noundef %27, i64 noundef %29)
  %31 = load i64, ptr %5, align 8
  %32 = add i64 %30, %31
  ret i64 %32
}

; Function Attrs: nounwind
define dso_local i64 @F178(ptr noundef %0, i64 noundef %1) #0 {
  %3 = alloca ptr, align 8
  %4 = alloca i64, align 8
  %5 = alloca i64, align 8
  store ptr %0, ptr %3, align 8
  store i64 %1, ptr %4, align 8
  %6 = load ptr, ptr %3, align 8
  %7 = getelementptr inbounds %struct.CPU, ptr %6, i32 0, i32 0
  %8 = load i64, ptr %4, align 8
  %9 = lshr i64 %8, 2
  %10 = and i64 %9, 31
  %11 = getelementptr inbounds [32 x i64], ptr %7, i64 0, i64 %10
  %12 = load i64, ptr %11, align 8
  %13 = load i64, ptr %4, align 8
  %14 = add i64 %12, %13
  store i64 %14, ptr %5, align 8
  %15 = load i64, ptr %5, align 8
  %16 = mul i64 %15, 37
  %17 = load i64, ptr %5, align 8
  %18 = lshr i64 %17, 7
  %19 = xor i64 %16, %18
  store i64 %19, ptr %5, align 8
  %20 = load i64, ptr %5, align 8
  %21 = load ptr, ptr %3, align 8
  %22 = getelementptr inbounds %struct.CPU, ptr %21, i32 0, i32 0
  %23 = load i64, ptr %4, align 8
  %24 = lshr i64 %23, 2
  %25 = and i64 %24, 31
  %26 = getelementptr inbounds [32 x i64], ptr %22, i64 0, i64 %25
  store i64 %20, ptr %26, align 8
  %27 = load ptr, ptr %3, align 8
  %28 = load i64, ptr %4, align 8
  %29 = add i64 %28, 4
  %30 = call i64 @F177(ptr noundef %27, i64 noundef %29)
  %31 = load i64, ptr %5, align 8
  %32 = add i64 %30, %31
  ret i64 %32
}

; Function Attrs: nounwind
define dso_local i64 @F179(ptr noundef %0, i64 noundef %1) #0 {
  %3 = alloca ptr, align 8
  %4 = alloca i64, align 8
  %5 = alloca i64, align 8
  store ptr %0, ptr %3, align 8
  store i64 %1, ptr %4, align 8
  %6 = load ptr, ptr %3, align 8
  %7 = getelementptr inbounds %struct.CPU, ptr %6, i32 0, i32 0
  %8 = load i64, ptr %4, align 8
  %9 = lshr i64 %8, 2
  %10 = and i64 %9, 31
  %11 = getelementptr inbounds [32 x i64], ptr %7, i64 0, i64 %10
  %12 = load i64, ptr %11, align 8
  %13 = load i64, ptr %4, align 8
  %14 = add i64 %12, %13
  store i64 %14, ptr %5, align 8
  %15 = load i64, ptr %5, align 8
  %16 = mul i64 %15, 37
  %17 = load i64, ptr %5, align 8
  %18 = lshr i64 %17, 7
  %19 = xor i64 %16, %18
  store i64 %19, ptr %5, align 8
  %20 = load i64, ptr %5, align 8
  %21 = load ptr, ptr %3, align 8
  %22 = getelementptr inbounds %struct.CPU, ptr %21, i32 0, i32 0
  %23 = load i64, ptr %4, align 8
  %24 = lshr i64 %23, 2
  %25 = and i64 %24, 31
  %26 = getelementptr inbounds [32 x i64], ptr %22, i64 0, i64 %25
  store i64 %20, ptr %26, align 8
  %27 = load ptr, ptr %3, align 8
  %28 = load i64, ptr %4, align 8
  %29 = add i64 %28, 4
  %30 = call i64 @F178(ptr noundef %27, i64 noundef %29)
  %31 = load i64, ptr %5, align 8
  %32 = add i64 %30, %31
  ret i64 %32
}

; Function Attrs: nounwind
define dso_local i64 @F180(ptr noundef %0, i64 noundef %1) #0 {
  %3 = alloca ptr, align 8
  %4 = alloca i64, align 8
  %5 = alloca i64, align 8
  store ptr %0, ptr %3, align 8
  store i64 %1, ptr %4, align 8
  %6 = load ptr, ptr %3, align 8
  %7 = getelementptr inbounds %struct.CPU, ptr %6, i32 0, i32 0
  %8 = load i64, ptr %4, align 8
  %9 = lshr i64 %8, 2
  %10 = and i64 %9, 31
  %11 = getelementptr inbounds [32 x i64], ptr %7, i64 0, i64 %10
  %12 = load i64, ptr %11, align 8
  %13 = load i64, ptr %4, align 8
  %14 = add i64 %12, %13
  store i64 %14, ptr %5, align 8
  %15 = load i64, ptr %5, align 8
  %16 = mul i64 %15, 37
  %17 = load i64, ptr %5, align 8
  %18 = lshr i64 %17, 7
  %19 = xor i64 %16, %18
  store i64 %19, ptr %5, align 8
  %20 = load i64, ptr %5, align 8
  %21 = load ptr, ptr %3, align 8
  %22 = getelementptr inbounds %struct.CPU, ptr %21, i32 0, i32 0
  %23 = load i64, ptr %4, align 8
  %24 = lshr i64 %23, 2
  %25 = and i64 %24, 31
  %26 = getelementptr inbounds [32 x i64], ptr %22, i64 0, i64 %25
  store i64 %20, ptr %26, align 8
  %27 = load ptr, ptr %3, align 8
  %28 = load i64, ptr %4, align 8
  %29 = add i64 %28, 4
  %30 = call i64 @F179(ptr noundef %27, i64 noundef %29)
  %31 = load i64, ptr %5, align 8
  %32 = add i64 %30, %31
  ret i64 %32
}

; Function Attrs: nounwind
define dso_local i64 @F181(ptr noundef %0, i64 noundef %1) #0 {
  %3 = alloca ptr, align 8
  %4 = alloca i64, align 8
  %5 = alloca i64, align 8
  store ptr %0, ptr %3, align 8
  store i64 %1, ptr %4, align 8
  %6 = load ptr, ptr %3, align 8
  %7 = getelementptr inbounds %struct.CPU, ptr %6, i32 0, i32 0
  %8 = load i64, ptr %4, align 8
  %9 = lshr i64 %8, 2
  %10 = and i64 %9, 31
  %11 = getelementptr inbounds [32 x i64], ptr %7, i64 0, i64 %10
  %12 = load i64, ptr %11, align 8
  %13 = load i64, ptr %4, align 8
  %14 = add i64 %12, %13
  store i64 %14, ptr %5, align 8
  %15 = load i64, ptr %5, align 8
  %16 = mul i64 %15, 37
  %17 = load i64, ptr %5, align 8
  %18 = lshr i64 %17, 7
  %19 = xor i64 %16, %18
  store i64 %19, ptr %5, align 8
  %20 = load i64, ptr %5, align 8
  %21 = load ptr, ptr %3, align 8
  %22 = getelementptr inbounds %struct.CPU, ptr %21, i32 0, i32 0
  %23 = load i64, ptr %4, align 8
  %24 = lshr i64 %23, 2
  %25 = and i64 %24, 31
  %26 = getelementptr inbounds [32 x i64], ptr %22, i64 0, i64 %25
  store i64 %20, ptr %26, align 8
  %27 = load ptr, ptr %3, align 8
  %28 = load i64, ptr %4, align 8
  %29 = add i64 %28, 4
  %30 = call i64 @F180(ptr noundef %27, i64 noundef %29)
  %31 = load i64, ptr %5, align 8
  %32 = add i64 %30, %31
  ret i64 %32
}

; Function Attrs: nounwind
define dso_local i64 @F182(ptr noundef %0, i64 noundef %1) #0 {
  %3 = alloca ptr, align 8
  %4 = alloca i64, align 8
  %5 = alloca i64, align 8
  store ptr %0, ptr %3, align 8
  store i64 %1, ptr %4, align 8
  %6 = load ptr, ptr %3, align 8
  %7 = getelementptr inbounds %struct.CPU, ptr %6, i32 0, i32 0
  %8 = load i64, ptr %4, align 8
  %9 = lshr i64 %8, 2
  %10 = and i64 %9, 31
  %11 = getelementptr inbounds [32 x i64], ptr %7, i64 0, i64 %10
  %12 = load i64, ptr %11, align 8
  %13 = load i64, ptr %4, align 8
  %14 = add i64 %12, %13
  store i64 %14, ptr %5, align 8
  %15 = load i64, ptr %5, align 8
  %16 = mul i64 %15, 37
  %17 = load i64, ptr %5, align 8
  %18 = lshr i64 %17, 7
  %19 = xor i64 %16, %18
  store i64 %19, ptr %5, align 8
  %20 = load i64, ptr %5, align 8
  %21 = load ptr, ptr %3, align 8
  %22 = getelementptr inbounds %struct.CPU, ptr %21, i32 0, i32 0
  %23 = load i64, ptr %4, align 8
  %24 = lshr i64 %23, 2
  %25 = and i64 %24, 31
  %26 = getelementptr inbounds [32 x i64], ptr %22, i64 0, i64 %25
  store i64 %20, ptr %26, align 8
  %27 = load ptr, ptr %3, align 8
  %28 = load i64, ptr %4, align 8
  %29 = add i64 %28, 4
  %30 = call i64 @F181(ptr noundef %27, i64 noundef %29)
  %31 = load i64, ptr %5, align 8
  %32 = add i64 %30, %31
  ret i64 %32
}

; Function Attrs: nounwind
define dso_local i64 @F183(ptr noundef %0, i64 noundef %1) #0 {
  %3 = alloca ptr, align 8
  %4 = alloca i64, align 8
  %5 = alloca i64, align 8
  store ptr %0, ptr %3, align 8
  store i64 %1, ptr %4, align 8
  %6 = load ptr, ptr %3, align 8
  %7 = getelementptr inbounds %struct.CPU, ptr %6, i32 0, i32 0
  %8 = load i64, ptr %4, align 8
  %9 = lshr i64 %8, 2
  %10 = and i64 %9, 31
  %11 = getelementptr inbounds [32 x i64], ptr %7, i64 0, i64 %10
  %12 = load i64, ptr %11, align 8
  %13 = load i64, ptr %4, align 8
  %14 = add i64 %12, %13
  store i64 %14, ptr %5, align 8
  %15 = load i64, ptr %5, align 8
  %16 = mul i64 %15, 37
  %17 = load i64, ptr %5, align 8
  %18 = lshr i64 %17, 7
  %19 = xor i64 %16, %18
  store i64 %19, ptr %5, align 8
  %20 = load i64, ptr %5, align 8
  %21 = load ptr, ptr %3, align 8
  %22 = getelementptr inbounds %struct.CPU, ptr %21, i32 0, i32 0
  %23 = load i64, ptr %4, align 8
  %24 = lshr i64 %23, 2
  %25 = and i64 %24, 31
  %26 = getelementptr inbounds [32 x i64], ptr %22, i64 0, i64 %25
  store i64 %20, ptr %26, align 8
  %27 = load ptr, ptr %3, align 8
  %28 = load i64, ptr %4, align 8
  %29 = add i64 %28, 4
  %30 = call i64 @F182(ptr noundef %27, i64 noundef %29)
  %31 = load i64, ptr %5, align 8
  %32 = add i64 %30, %31
  ret i64 %32
}

; Function Attrs: nounwind
define dso_local i64 @F184(ptr noundef %0, i64 noundef %1) #0 {
  %3 = alloca ptr, align 8
  %4 = alloca i64, align 8
  %5 = alloca i64, align 8
  store ptr %0, ptr %3, align 8
  store i64 %1, ptr %4, align 8
  %6 = load ptr, ptr %3, align 8
  %7 = getelementptr inbounds %struct.CPU, ptr %6, i32 0, i32 0
  %8 = load i64, ptr %4, align 8
  %9 = lshr i64 %8, 2
  %10 = and i64 %9, 31
  %11 = getelementptr inbounds [32 x i64], ptr %7, i64 0, i64 %10
  %12 = load i64, ptr %11, align 8
  %13 = load i64, ptr %4, align 8
  %14 = add i64 %12, %13
  store i64 %14, ptr %5, align 8
  %15 = load i64, ptr %5, align 8
  %16 = mul i64 %15, 37
  %17 = load i64, ptr %5, align 8
  %18 = lshr i64 %17, 7
  %19 = xor i64 %16, %18
  store i64 %19, ptr %5, align 8
  %20 = load i64, ptr %5, align 8
  %21 = load ptr, ptr %3, align 8
  %22 = getelementptr inbounds %struct.CPU, ptr %21, i32 0, i32 0
  %23 = load i64, ptr %4, align 8
  %24 = lshr i64 %23, 2
  %25 = and i64 %24, 31
  %26 = getelementptr inbounds [32 x i64], ptr %22, i64 0, i64 %25
  store i64 %20, ptr %26, align 8
  %27 = load ptr, ptr %3, align 8
  %28 = load i64, ptr %4, align 8
  %29 = add i64 %28, 4
  %30 = call i64 @F183(ptr noundef %27, i64 noundef %29)
  %31 = load i64, ptr %5, align 8
  %32 = add i64 %30, %31
  ret i64 %32
}

; Function Attrs: nounwind
define dso_local i64 @F185(ptr noundef %0, i64 noundef %1) #0 {
  %3 = alloca ptr, align 8
  %4 = alloca i64, align 8
  %5 = alloca i64, align 8
  store ptr %0, ptr %3, align 8
  store i64 %1, ptr %4, align 8
  %6 = load ptr, ptr %3, align 8
  %7 = getelementptr inbounds %struct.CPU, ptr %6, i32 0, i32 0
  %8 = load i64, ptr %4, align 8
  %9 = lshr i64 %8, 2
  %10 = and i64 %9, 31
  %11 = getelementptr inbounds [32 x i64], ptr %7, i64 0, i64 %10
  %12 = load i64, ptr %11, align 8
  %13 = load i64, ptr %4, align 8
  %14 = add i64 %12, %13
  store i64 %14, ptr %5, align 8
  %15 = load i64, ptr %5, align 8
  %16 = mul i64 %15, 37
  %17 = load i64, ptr %5, align 8
  %18 = lshr i64 %17, 7
  %19 = xor i64 %16, %18
  store i64 %19, ptr %5, align 8
  %20 = load i64, ptr %5, align 8
  %21 = load ptr, ptr %3, align 8
  %22 = getelementptr inbounds %struct.CPU, ptr %21, i32 0, i32 0
  %23 = load i64, ptr %4, align 8
  %24 = lshr i64 %23, 2
  %25 = and i64 %24, 31
  %26 = getelementptr inbounds [32 x i64], ptr %22, i64 0, i64 %25
  store i64 %20, ptr %26, align 8
  %27 = load ptr, ptr %3, align 8
  %28 = load i64, ptr %4, align 8
  %29 = add i64 %28, 4
  %30 = call i64 @F184(ptr noundef %27, i64 noundef %29)
  %31 = load i64, ptr %5, align 8
  %32 = add i64 %30, %31
  ret i64 %32
}

; Function Attrs: nounwind
define dso_local i64 @F186(ptr noundef %0, i64 noundef %1) #0 {
  %3 = alloca ptr, align 8
  %4 = alloca i64, align 8
  %5 = alloca i64, align 8
  store ptr %0, ptr %3, align 8
  store i64 %1, ptr %4, align 8
  %6 = load ptr, ptr %3, align 8
  %7 = getelementptr inbounds %struct.CPU, ptr %6, i32 0, i32 0
  %8 = load i64, ptr %4, align 8
  %9 = lshr i64 %8, 2
  %10 = and i64 %9, 31
  %11 = getelementptr inbounds [32 x i64], ptr %7, i64 0, i64 %10
  %12 = load i64, ptr %11, align 8
  %13 = load i64, ptr %4, align 8
  %14 = add i64 %12, %13
  store i64 %14, ptr %5, align 8
  %15 = load i64, ptr %5, align 8
  %16 = mul i64 %15, 37
  %17 = load i64, ptr %5, align 8
  %18 = lshr i64 %17, 7
  %19 = xor i64 %16, %18
  store i64 %19, ptr %5, align 8
  %20 = load i64, ptr %5, align 8
  %21 = load ptr, ptr %3, align 8
  %22 = getelementptr inbounds %struct.CPU, ptr %21, i32 0, i32 0
  %23 = load i64, ptr %4, align 8
  %24 = lshr i64 %23, 2
  %25 = and i64 %24, 31
  %26 = getelementptr inbounds [32 x i64], ptr %22, i64 0, i64 %25
  store i64 %20, ptr %26, align 8
  %27 = load ptr, ptr %3, align 8
  %28 = load i64, ptr %4, align 8
  %29 = add i64 %28, 4
  %30 = call i64 @F185(ptr noundef %27, i64 noundef %29)
  %31 = load i64, ptr %5, align 8
  %32 = add i64 %30, %31
  ret i64 %32
}

; Function Attrs: nounwind
define dso_local i64 @F187(ptr noundef %0, i64 noundef %1) #0 {
  %3 = alloca ptr, align 8
  %4 = alloca i64, align 8
  %5 = alloca i64, align 8
  store ptr %0, ptr %3, align 8
  store i64 %1, ptr %4, align 8
  %6 = load ptr, ptr %3, align 8
  %7 = getelementptr inbounds %struct.CPU, ptr %6, i32 0, i32 0
  %8 = load i64, ptr %4, align 8
  %9 = lshr i64 %8, 2
  %10 = and i64 %9, 31
  %11 = getelementptr inbounds [32 x i64], ptr %7, i64 0, i64 %10
  %12 = load i64, ptr %11, align 8
  %13 = load i64, ptr %4, align 8
  %14 = add i64 %12, %13
  store i64 %14, ptr %5, align 8
  %15 = load i64, ptr %5, align 8
  %16 = mul i64 %15, 37
  %17 = load i64, ptr %5, align 8
  %18 = lshr i64 %17, 7
  %19 = xor i64 %16, %18
  store i64 %19, ptr %5, align 8
  %20 = load i64, ptr %5, align 8
  %21 = load ptr, ptr %3, align 8
  %22 = getelementptr inbounds %struct.CPU, ptr %21, i32 0, i32 0
  %23 = load i64, ptr %4, align 8
  %24 = lshr i64 %23, 2
  %25 = and i64 %24, 31
  %26 = getelementptr inbounds [32 x i64], ptr %22, i64 0, i64 %25
  store i64 %20, ptr %26, align 8
  %27 = load ptr, ptr %3, align 8
  %28 = load i64, ptr %4, align 8
  %29 = add i64 %28, 4
  %30 = call i64 @F186(ptr noundef %27, i64 noundef %29)
  %31 = load i64, ptr %5, align 8
  %32 = add i64 %30, %31
  ret i64 %32
}

; Function Attrs: nounwind
define dso_local i64 @F188(ptr noundef %0, i64 noundef %1) #0 {
  %3 = alloca ptr, align 8
  %4 = alloca i64, align 8
  %5 = alloca i64, align 8
  store ptr %0, ptr %3, align 8
  store i64 %1, ptr %4, align 8
  %6 = load ptr, ptr %3, align 8
  %7 = getelementptr inbounds %struct.CPU, ptr %6, i32 0, i32 0
  %8 = load i64, ptr %4, align 8
  %9 = lshr i64 %8, 2
  %10 = and i64 %9, 31
  %11 = getelementptr inbounds [32 x i64], ptr %7, i64 0, i64 %10
  %12 = load i64, ptr %11, align 8
  %13 = load i64, ptr %4, align 8
  %14 = add i64 %12, %13
  store i64 %14, ptr %5, align 8
  %15 = load i64, ptr %5, align 8
  %16 = mul i64 %15, 37
  %17 = load i64, ptr %5, align 8
  %18 = lshr i64 %17, 7
  %19 = xor i64 %16, %18
  store i64 %19, ptr %5, align 8
  %20 = load i64, ptr %5, align 8
  %21 = load ptr, ptr %3, align 8
  %22 = getelementptr inbounds %struct.CPU, ptr %21, i32 0, i32 0
  %23 = load i64, ptr %4, align 8
  %24 = lshr i64 %23, 2
  %25 = and i64 %24, 31
  %26 = getelementptr inbounds [32 x i64], ptr %22, i64 0, i64 %25
  store i64 %20, ptr %26, align 8
  %27 = load ptr, ptr %3, align 8
  %28 = load i64, ptr %4, align 8
  %29 = add i64 %28, 4
  %30 = call i64 @F187(ptr noundef %27, i64 noundef %29)
  %31 = load i64, ptr %5, align 8
  %32 = add i64 %30, %31
  ret i64 %32
}

; Function Attrs: nounwind
define dso_local i64 @F189(ptr noundef %0, i64 noundef %1) #0 {
  %3 = alloca ptr, align 8
  %4 = alloca i64, align 8
  %5 = alloca i64, align 8
  store ptr %0, ptr %3, align 8
  store i64 %1, ptr %4, align 8
  %6 = load ptr, ptr %3, align 8
  %7 = getelementptr inbounds %struct.CPU, ptr %6, i32 0, i32 0
  %8 = load i64, ptr %4, align 8
  %9 = lshr i64 %8, 2
  %10 = and i64 %9, 31
  %11 = getelementptr inbounds [32 x i64], ptr %7, i64 0, i64 %10
  %12 = load i64, ptr %11, align 8
  %13 = load i64, ptr %4, align 8
  %14 = add i64 %12, %13
  store i64 %14, ptr %5, align 8
  %15 = load i64, ptr %5, align 8
  %16 = mul i64 %15, 37
  %17 = load i64, ptr %5, align 8
  %18 = lshr i64 %17, 7
  %19 = xor i64 %16, %18
  store i64 %19, ptr %5, align 8
  %20 = load i64, ptr %5, align 8
  %21 = load ptr, ptr %3, align 8
  %22 = getelementptr inbounds %struct.CPU, ptr %21, i32 0, i32 0
  %23 = load i64, ptr %4, align 8
  %24 = lshr i64 %23, 2
  %25 = and i64 %24, 31
  %26 = getelementptr inbounds [32 x i64], ptr %22, i64 0, i64 %25
  store i64 %20, ptr %26, align 8
  %27 = load ptr, ptr %3, align 8
  %28 = load i64, ptr %4, align 8
  %29 = add i64 %28, 4
  %30 = call i64 @F188(ptr noundef %27, i64 noundef %29)
  %31 = load i64, ptr %5, align 8
  %32 = add i64 %30, %31
  ret i64 %32
}

; Function Attrs: nounwind
define dso_local i64 @F190(ptr noundef %0, i64 noundef %1) #0 {
  %3 = alloca ptr, align 8
  %4 = alloca i64, align 8
  %5 = alloca i64, align 8
  store ptr %0, ptr %3, align 8
  store i64 %1, ptr %4, align 8
  %6 = load ptr, ptr %3, align 8
  %7 = getelementptr inbounds %struct.CPU, ptr %6, i32 0, i32 0
  %8 = load i64, ptr %4, align 8
  %9 = lshr i64 %8, 2
  %10 = and i64 %9, 31
  %11 = getelementptr inbounds [32 x i64], ptr %7, i64 0, i64 %10
  %12 = load i64, ptr %11, align 8
  %13 = load i64, ptr %4, align 8
  %14 = add i64 %12, %13
  store i64 %14, ptr %5, align 8
  %15 = load i64, ptr %5, align 8
  %16 = mul i64 %15, 37
  %17 = load i64, ptr %5, align 8
  %18 = lshr i64 %17, 7
  %19 = xor i64 %16, %18
  store i64 %19, ptr %5, align 8
  %20 = load i64, ptr %5, align 8
  %21 = load ptr, ptr %3, align 8
  %22 = getelementptr inbounds %struct.CPU, ptr %21, i32 0, i32 0
  %23 = load i64, ptr %4, align 8
  %24 = lshr i64 %23, 2
  %25 = and i64 %24, 31
  %26 = getelementptr inbounds [32 x i64], ptr %22, i64 0, i64 %25
  store i64 %20, ptr %26, align 8
  %27 = load ptr, ptr %3, align 8
  %28 = load i64, ptr %4, align 8
  %29 = add i64 %28, 4
  %30 = call i64 @F189(ptr noundef %27, i64 noundef %29)
  %31 = load i64, ptr %5, align 8
  %32 = add i64 %30, %31
  ret i64 %32
}

; Function Attrs: nounwind
define dso_local i64 @F191(ptr noundef %0, i64 noundef %1) #0 {
  %3 = alloca ptr, align 8
  %4 = alloca i64, align 8
  %5 = alloca i64, align 8
  store ptr %0, ptr %3, align 8
  store i64 %1, ptr %4, align 8
  %6 = load ptr, ptr %3, align 8
  %7 = getelementptr inbounds %struct.CPU, ptr %6, i32 0, i32 0
  %8 = load i64, ptr %4, align 8
  %9 = lshr i64 %8, 2
  %10 = and i64 %9, 31
  %11 = getelementptr inbounds [32 x i64], ptr %7, i64 0, i64 %10
  %12 = load i64, ptr %11, align 8
  %13 = load i64, ptr %4, align 8
  %14 = add i64 %12, %13
  store i64 %14, ptr %5, align 8
  %15 = load i64, ptr %5, align 8
  %16 = mul i64 %15, 37
  %17 = load i64, ptr %5, align 8
  %18 = lshr i64 %17, 7
  %19 = xor i64 %16, %18
  store i64 %19, ptr %5, align 8
  %20 = load i64, ptr %5, align 8
  %21 = load ptr, ptr %3, align 8
  %22 = getelementptr inbounds %struct.CPU, ptr %21, i32 0, i32 0
  %23 = load i64, ptr %4, align 8
  %24 = lshr i64 %23, 2
  %25 = and i64 %24, 31
  %26 = getelementptr inbounds [32 x i64], ptr %22, i64 0, i64 %25
  store i64 %20, ptr %26, align 8
  %27 = load ptr, ptr %3, align 8
  %28 = load i64, ptr %4, align 8
  %29 = add i64 %28, 4
  %30 = call i64 @F190(ptr noundef %27, i64 noundef %29)
  %31 = load i64, ptr %5, align 8
  %32 = add i64 %30, %31
  ret i64 %32
}

; Function Attrs: nounwind
define dso_local i64 @F192(ptr noundef %0, i64 noundef %1) #0 {
  %3 = alloca ptr, align 8
  %4 = alloca i64, align 8
  %5 = alloca i64, align 8
  store ptr %0, ptr %3, align 8
  store i64 %1, ptr %4, align 8
  %6 = load ptr, ptr %3, align 8
  %7 = getelementptr inbounds %struct.CPU, ptr %6, i32 0, i32 0
  %8 = load i64, ptr %4, align 8
  %9 = lshr i64 %8, 2
  %10 = and i64 %9, 31
  %11 = getelementptr inbounds [32 x i64], ptr %7, i64 0, i64 %10
  %12 = load i64, ptr %11, align 8
  %13 = load i64, ptr %4, align 8
  %14 = add i64 %12, %13
  store i64 %14, ptr %5, align 8
  %15 = load i64, ptr %5, align 8
  %16 = mul i64 %15, 37
  %17 = load i64, ptr %5, align 8
  %18 = lshr i64 %17, 7
  %19 = xor i64 %16, %18
  store i64 %19, ptr %5, align 8
  %20 = load i64, ptr %5, align 8
  %21 = load ptr, ptr %3, align 8
  %22 = getelementptr inbounds %struct.CPU, ptr %21, i32 0, i32 0
  %23 = load i64, ptr %4, align 8
  %24 = lshr i64 %23, 2
  %25 = and i64 %24, 31
  %26 = getelementptr inbounds [32 x i64], ptr %22, i64 0, i64 %25
  store i64 %20, ptr %26, align 8
  %27 = load ptr, ptr %3, align 8
  %28 = load i64, ptr %4, align 8
  %29 = add i64 %28, 4
  %30 = call i64 @F191(ptr noundef %27, i64 noundef %29)
  %31 = load i64, ptr %5, align 8
  %32 = add i64 %30, %31
  ret i64 %32
}

; Function Attrs: nounwind
define dso_local i64 @F193(ptr noundef %0, i64 noundef %1) #0 {
  %3 = alloca ptr, align 8
  %4 = alloca i64, align 8
  %5 = alloca i64, align 8
  store ptr %0, ptr %3, align 8
  store i64 %1, ptr %4, align 8
  %6 = load ptr, ptr %3, align 8
  %7 = getelementptr inbounds %struct.CPU, ptr %6, i32 0, i32 0
  %8 = load i64, ptr %4, align 8
  %9 = lshr i64 %8, 2
  %10 = and i64 %9, 31
  %11 = getelementptr inbounds [32 x i64], ptr %7, i64 0, i64 %10
  %12 = load i64, ptr %11, align 8
  %13 = load i64, ptr %4, align 8
  %14 = add i64 %12, %13
  store i64 %14, ptr %5, align 8
  %15 = load i64, ptr %5, align 8
  %16 = mul i64 %15, 37
  %17 = load i64, ptr %5, align 8
  %18 = lshr i64 %17, 7
  %19 = xor i64 %16, %18
  store i64 %19, ptr %5, align 8
  %20 = load i64, ptr %5, align 8
  %21 = load ptr, ptr %3, align 8
  %22 = getelementptr inbounds %struct.CPU, ptr %21, i32 0, i32 0
  %23 = load i64, ptr %4, align 8
  %24 = lshr i64 %23, 2
  %25 = and i64 %24, 31
  %26 = getelementptr inbounds [32 x i64], ptr %22, i64 0, i64 %25
  store i64 %20, ptr %26, align 8
  %27 = load ptr, ptr %3, align 8
  %28 = load i64, ptr %4, align 8
  %29 = add i64 %28, 4
  %30 = call i64 @F192(ptr noundef %27, i64 noundef %29)
  %31 = load i64, ptr %5, align 8
  %32 = add i64 %30, %31
  ret i64 %32
}

; Function Attrs: nounwind
define dso_local i64 @F194(ptr noundef %0, i64 noundef %1) #0 {
  %3 = alloca ptr, align 8
  %4 = alloca i64, align 8
  %5 = alloca i64, align 8
  store ptr %0, ptr %3, align 8
  store i64 %1, ptr %4, align 8
  %6 = load ptr, ptr %3, align 8
  %7 = getelementptr inbounds %struct.CPU, ptr %6, i32 0, i32 0
  %8 = load i64, ptr %4, align 8
  %9 = lshr i64 %8, 2
  %10 = and i64 %9, 31
  %11 = getelementptr inbounds [32 x i64], ptr %7, i64 0, i64 %10
  %12 = load i64, ptr %11, align 8
  %13 = load i64, ptr %4, align 8
  %14 = add i64 %12, %13
  store i64 %14, ptr %5, align 8
  %15 = load i64, ptr %5, align 8
  %16 = mul i64 %15, 37
  %17 = load i64, ptr %5, align 8
  %18 = lshr i64 %17, 7
  %19 = xor i64 %16, %18
  store i64 %19, ptr %5, align 8
  %20 = load i64, ptr %5, align 8
  %21 = load ptr, ptr %3, align 8
  %22 = getelementptr inbounds %struct.CPU, ptr %21, i32 0, i32 0
  %23 = load i64, ptr %4, align 8
  %24 = lshr i64 %23, 2
  %25 = and i64 %24, 31
  %26 = getelementptr inbounds [32 x i64], ptr %22, i64 0, i64 %25
  store i64 %20, ptr %26, align 8
  %27 = load ptr, ptr %3, align 8
  %28 = load i64, ptr %4, align 8
  %29 = add i64 %28, 4
  %30 = call i64 @F193(ptr noundef %27, i64 noundef %29)
  %31 = load i64, ptr %5, align 8
  %32 = add i64 %30, %31
  ret i64 %32
}

; Function Attrs: nounwind
define dso_local i64 @F195(ptr noundef %0, i64 noundef %1) #0 {
  %3 = alloca ptr, align 8
  %4 = alloca i64, align 8
  %5 = alloca i64, align 8
  store ptr %0, ptr %3, align 8
  store i64 %1, ptr %4, align 8
  %6 = load ptr, ptr %3, align 8
  %7 = getelementptr inbounds %struct.CPU, ptr %6, i32 0, i32 0
  %8 = load i64, ptr %4, align 8
  %9 = lshr i64 %8, 2
  %10 = and i64 %9, 31
  %11 = getelementptr inbounds [32 x i64], ptr %7, i64 0, i64 %10
  %12 = load i64, ptr %11, align 8
  %13 = load i64, ptr %4, align 8
  %14 = add i64 %12, %13
  store i64 %14, ptr %5, align 8
  %15 = load i64, ptr %5, align 8
  %16 = mul i64 %15, 37
  %17 = load i64, ptr %5, align 8
  %18 = lshr i64 %17, 7
  %19 = xor i64 %16, %18
  store i64 %19, ptr %5, align 8
  %20 = load i64, ptr %5, align 8
  %21 = load ptr, ptr %3, align 8
  %22 = getelementptr inbounds %struct.CPU, ptr %21, i32 0, i32 0
  %23 = load i64, ptr %4, align 8
  %24 = lshr i64 %23, 2
  %25 = and i64 %24, 31
  %26 = getelementptr inbounds [32 x i64], ptr %22, i64 0, i64 %25
  store i64 %20, ptr %26, align 8
  %27 = load ptr, ptr %3, align 8
  %28 = load i64, ptr %4, align 8
  %29 = add i64 %28, 4
  %30 = call i64 @F194(ptr noundef %27, i64 noundef %29)
  %31 = load i64, ptr %5, align 8
  %32 = add i64 %30, %31
  ret i64 %32
}

; Function Attrs: nounwind
define dso_local i64 @F196(ptr noundef %0, i64 noundef %1) #0 {
  %3 = alloca ptr, align 8
  %4 = alloca i64, align 8
  %5 = alloca i64, align 8
  store ptr %0, ptr %3, align 8
  store i64 %1, ptr %4, align 8
  %6 = load ptr, ptr %3, align 8
  %7 = getelementptr inbounds %struct.CPU, ptr %6, i32 0, i32 0
  %8 = load i64, ptr %4, align 8
  %9 = lshr i64 %8, 2
  %10 = and i64 %9, 31
  %11 = getelementptr inbounds [32 x i64], ptr %7, i64 0, i64 %10
  %12 = load i64, ptr %11, align 8
  %13 = load i64, ptr %4, align 8
  %14 = add i64 %12, %13
  store i64 %14, ptr %5, align 8
  %15 = load i64, ptr %5, align 8
  %16 = mul i64 %15, 37
  %17 = load i64, ptr %5, align 8
  %18 = lshr i64 %17, 7
  %19 = xor i64 %16, %18
  store i64 %19, ptr %5, align 8
  %20 = load i64, ptr %5, align 8
  %21 = load ptr, ptr %3, align 8
  %22 = getelementptr inbounds %struct.CPU, ptr %21, i32 0, i32 0
  %23 = load i64, ptr %4, align 8
  %24 = lshr i64 %23, 2
  %25 = and i64 %24, 31
  %26 = getelementptr inbounds [32 x i64], ptr %22, i64 0, i64 %25
  store i64 %20, ptr %26, align 8
  %27 = load ptr, ptr %3, align 8
  %28 = load i64, ptr %4, align 8
  %29 = add i64 %28, 4
  %30 = call i64 @F195(ptr noundef %27, i64 noundef %29)
  %31 = load i64, ptr %5, align 8
  %32 = add i64 %30, %31
  ret i64 %32
}

; Function Attrs: nounwind
define dso_local i64 @F197(ptr noundef %0, i64 noundef %1) #0 {
  %3 = alloca ptr, align 8
  %4 = alloca i64, align 8
  %5 = alloca i64, align 8
  store ptr %0, ptr %3, align 8
  store i64 %1, ptr %4, align 8
  %6 = load ptr, ptr %3, align 8
  %7 = getelementptr inbounds %struct.CPU, ptr %6, i32 0, i32 0
  %8 = load i64, ptr %4, align 8
  %9 = lshr i64 %8, 2
  %10 = and i64 %9, 31
  %11 = getelementptr inbounds [32 x i64], ptr %7, i64 0, i64 %10
  %12 = load i64, ptr %11, align 8
  %13 = load i64, ptr %4, align 8
  %14 = add i64 %12, %13
  store i64 %14, ptr %5, align 8
  %15 = load i64, ptr %5, align 8
  %16 = mul i64 %15, 37
  %17 = load i64, ptr %5, align 8
  %18 = lshr i64 %17, 7
  %19 = xor i64 %16, %18
  store i64 %19, ptr %5, align 8
  %20 = load i64, ptr %5, align 8
  %21 = load ptr, ptr %3, align 8
  %22 = getelementptr inbounds %struct.CPU, ptr %21, i32 0, i32 0
  %23 = load i64, ptr %4, align 8
  %24 = lshr i64 %23, 2
  %25 = and i64 %24, 31
  %26 = getelementptr inbounds [32 x i64], ptr %22, i64 0, i64 %25
  store i64 %20, ptr %26, align 8
  %27 = load ptr, ptr %3, align 8
  %28 = load i64, ptr %4, align 8
  %29 = add i64 %28, 4
  %30 = call i64 @F196(ptr noundef %27, i64 noundef %29)
  %31 = load i64, ptr %5, align 8
  %32 = add i64 %30, %31
  ret i64 %32
}

; Function Attrs: nounwind
define dso_local i64 @F198(ptr noundef %0, i64 noundef %1) #0 {
  %3 = alloca ptr, align 8
  %4 = alloca i64, align 8
  %5 = alloca i64, align 8
  store ptr %0, ptr %3, align 8
  store i64 %1, ptr %4, align 8
  %6 = load ptr, ptr %3, align 8
  %7 = getelementptr inbounds %struct.CPU, ptr %6, i32 0, i32 0
  %8 = load i64, ptr %4, align 8
  %9 = lshr i64 %8, 2
  %10 = and i64 %9, 31
  %11 = getelementptr inbounds [32 x i64], ptr %7, i64 0, i64 %10
  %12 = load i64, ptr %11, align 8
  %13 = load i64, ptr %4, align 8
  %14 = add i64 %12, %13
  store i64 %14, ptr %5, align 8
  %15 = load i64, ptr %5, align 8
  %16 = mul i64 %15, 37
  %17 = load i64, ptr %5, align 8
  %18 = lshr i64 %17, 7
  %19 = xor i64 %16, %18
  store i64 %19, ptr %5, align 8
  %20 = load i64, ptr %5, align 8
  %21 = load ptr, ptr %3, align 8
  %22 = getelementptr inbounds %struct.CPU, ptr %21, i32 0, i32 0
  %23 = load i64, ptr %4, align 8
  %24 = lshr i64 %23, 2
  %25 = and i64 %24, 31
  %26 = getelementptr inbounds [32 x i64], ptr %22, i64 0, i64 %25
  store i64 %20, ptr %26, align 8
  %27 = load ptr, ptr %3, align 8
  %28 = load i64, ptr %4, align 8
  %29 = add i64 %28, 4
  %30 = call i64 @F197(ptr noundef %27, i64 noundef %29)
  %31 = load i64, ptr %5, align 8
  %32 = add i64 %30, %31
  ret i64 %32
}

; Function Attrs: nounwind
define dso_local i64 @F199(ptr noundef %0, i64 noundef %1) #0 {
  %3 = alloca ptr, align 8
  %4 = alloca i64, align 8
  %5 = alloca i64, align 8
  store ptr %0, ptr %3, align 8
  store i64 %1, ptr %4, align 8
  %6 = load ptr, ptr %3, align 8
  %7 = getelementptr inbounds %struct.CPU, ptr %6, i32 0, i32 0
  %8 = load i64, ptr %4, align 8
  %9 = lshr i64 %8, 2
  %10 = and i64 %9, 31
  %11 = getelementptr inbounds [32 x i64], ptr %7, i64 0, i64 %10
  %12 = load i64, ptr %11, align 8
  %13 = load i64, ptr %4, align 8
  %14 = add i64 %12, %13
  store i64 %14, ptr %5, align 8
  %15 = load i64, ptr %5, align 8
  %16 = mul i64 %15, 37
  %17 = load i64, ptr %5, align 8
  %18 = lshr i64 %17, 7
  %19 = xor i64 %16, %18
  store i64 %19, ptr %5, align 8
  %20 = load i64, ptr %5, align 8
  %21 = load ptr, ptr %3, align 8
  %22 = getelementptr inbounds %struct.CPU, ptr %21, i32 0, i32 0
  %23 = load i64, ptr %4, align 8
  %24 = lshr i64 %23, 2
  %25 = and i64 %24, 31
  %26 = getelementptr inbounds [32 x i64], ptr %22, i64 0, i64 %25
  store i64 %20, ptr %26, align 8
  %27 = load ptr, ptr %3, align 8
  %28 = load i64, ptr %4, align 8
  %29 = add i64 %28, 4
  %30 = call i64 @F198(ptr noundef %27, i64 noundef %29)
  %31 = load i64, ptr %5, align 8
  %32 = add i64 %30, %31
  ret i64 %32
}

attributes #0 = { nounwind "frame-pointer"="all" "min-legal-vector-width"="0" "no-trapping-math"="true" "stack-protector-buffer-size"="8" "target-cpu"="x86-64" "target-features"="+cmov,+cx8,+fxsr,+mmx,+sse,+sse2,+x87" "tune-cpu"="generic" }

!llvm.module.flags = !{!0, !1, !2, !3}
!llvm.ident = !{!4}

!0 = !{i32 1, !"wchar_size", i32 4}
!1 = !{i32 8, !"PIC Level", i32 2}
!2 = !{i32 7, !"PIE Level", i32 2}
!3 = !{i32 7, !"frame-pointer", i32 2}
!4 = !{!"clang version 17.0.0 (https://github.com/swiftlang/llvm-project.git 10999b6d034fe318f3d56c83bddb6572593a8bb0)"}
