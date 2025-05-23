### Компиляция
```bash
gcc -pthread otp.c -o otp  
```

### Тесты
```bash
./otp -i test.txt -o crypt.bin -x 4212 -a 84589 -c 45989 -m 217728
./otp -i crypt.bin -o decrypt.txt -x 4212 -a 84589 -c 45989 -m 217728
    diff test.txt decrypt.txt
```
