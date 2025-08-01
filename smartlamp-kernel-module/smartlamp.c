#include <linux/mutex.h>
#include <linux/module.h>
#include <linux/usb.h>
#include <linux/slab.h>
#include <linux/leds.h>

MODULE_AUTHOR("DevTITANS <devtitans@icomp.ufam.edu.br>");
MODULE_DESCRIPTION("Driver de acesso ao SmartLamp (ESP32 com Chip Serial CP2102");
MODULE_LICENSE("GPL");


#define MAX_RECV_LINE 100 // Tamanho máximo de uma linha de resposta do dispositvo USB


static char recv_line[MAX_RECV_LINE];              // Armazena dados vindos da USB até receber um caractere de nova linha '\n'
static struct usb_device *smartlamp_device;        // Referência para o dispositivo USB
static uint usb_in, usb_out;                       // Endereços das portas de entrada e saida da USB
static char *usb_in_buffer, *usb_out_buffer;       // Buffers de entrada e saída da USB
static int usb_max_size;                           // Tamanho máximo de uma mensagem USB

#define VENDOR_ID   0x10c4 /* Encontre o VendorID  do smartlamp */
#define PRODUCT_ID   0xea60  /* Encontre o ProductID do smartlamp */
static const struct usb_device_id id_table[] = { { USB_DEVICE(VENDOR_ID, PRODUCT_ID) }, {} };

static int  usb_probe(struct usb_interface *ifce, const struct usb_device_id *id); // Executado quando o dispositivo é conectado na USB
static void usb_disconnect(struct usb_interface *ifce);                           // Executado quando o dispositivo USB é desconectado da USB
static int  usb_read_serial(int);
static int usb_send_cmd(char *cmd, int param); // Declaração antecipada

// Executado quando o arquivo /sys/kernel/smartlamp/{led, ldr} é lido (e.g., cat /sys/kernel/smartlamp/led)
static ssize_t attr_show(struct kobject *sys_obj, struct kobj_attribute *attr, char *buff);
// Executado quando o arquivo /sys/kernel/smartlamp/{led, ldr} é escrito (e.g., echo "100" | sudo tee -a /sys/kernel/smartlamp/led)
static ssize_t attr_store(struct kobject *sys_obj, struct kobj_attribute *attr, const char *buff, size_t count);
// Variáveis para criar os arquivos no /sys/kernel/smartlamp/{led, ldr}
static struct kobj_attribute  led_attribute = __ATTR(led, S_IRUGO | S_IWUSR, attr_show, attr_store);
static struct kobj_attribute  ldr_attribute = __ATTR(ldr, S_IRUGO | S_IWUSR, attr_show, attr_store);
static struct kobj_attribute  temp_attribute = __ATTR(temp, S_IRUGO | S_IWUSR, attr_show, attr_store);
static struct kobj_attribute  hum_attribute = __ATTR(hum, S_IRUGO | S_IWUSR, attr_show, attr_store);
static struct attribute      *attrs[]       = { &led_attribute.attr, &ldr_attribute.attr, &temp_attribute.attr, &hum_attribute.attr, NULL };
static struct attribute_group attr_group    = { .attrs = attrs };
static struct kobject        *sys_obj;                                             // Executado para ler a saida da porta serial

MODULE_DEVICE_TABLE(usb, id_table);

bool ignore = true;
int LDR_value = 0;

static struct usb_driver smartlamp_driver = {
    .name        = "smartlamp",     // Nome do driver
    .probe       = usb_probe,       // Executado quando o dispositivo é conectado na USB
    .disconnect  = usb_disconnect,  // Executado quando o dispositivo é desconectado na USB
    .id_table    = id_table,        // Tabela com o VendorID e ProductID do dispositivo
};

module_usb_driver(smartlamp_driver);

// Executado quando o dispositivo é conectado na USB
static struct led_classdev *led_cdev; // Ponteiro global para liberar na desconexão

static int led_brightness = -1;
static DEFINE_MUTEX(led_mutex);

// Função chamada ao escrever em /sys/class/leds/smartlamp_led/brightness
void led_set_brightness(struct led_classdev *led_cdev, unsigned int brightness) {
    int value = (int)brightness;
    mutex_lock(&led_mutex);
    led_brightness = value;
    // Envia comando para o dispositivo via USB
    char cmd[19];
    snprintf(cmd, sizeof(cmd), "SET_LED %d", value);
    usb_send_cmd(cmd, 2);
    mutex_unlock(&led_mutex);
    printk(KERN_INFO "SmartLamp: LED set brightness %d\n", value);
}

// Função chamada ao ler /sys/class/leds/smartlamp_led/brightness
static enum led_brightness led_get_brightness(struct led_classdev *led_cdev) {
    int value;
    mutex_lock(&led_mutex);
    value = usb_send_cmd("GET_LED", 1);
    led_brightness = value;
    mutex_unlock(&led_mutex);
    printk(KERN_INFO "SmartLamp: LED get brightness %d\n", value);
    return (enum led_brightness)value;
}

static int usb_probe(struct usb_interface *interface, const struct usb_device_id *id) {
    struct usb_endpoint_descriptor *usb_endpoint_in, *usb_endpoint_out;

    printk(KERN_INFO "SmartLamp: Dispositivo conectado ...\n");

    // Cria arquivos do /sys/kernel/smartlamp/*
    sys_obj = kobject_create_and_add("smartlamp", kernel_kobj);
    ignore = sysfs_create_group(sys_obj, &attr_group); // AQUI

    // Detecta portas e aloca buffers de entrada e saída de dados na USB
    smartlamp_device = interface_to_usbdev(interface);
    ignore =  usb_find_common_endpoints(interface->cur_altsetting, &usb_endpoint_in, &usb_endpoint_out, NULL, NULL);  // AQUI
    usb_max_size = usb_endpoint_maxp(usb_endpoint_in);
    usb_in = usb_endpoint_in->bEndpointAddress;
    usb_out = usb_endpoint_out->bEndpointAddress;
    usb_in_buffer = kmalloc(usb_max_size, GFP_KERNEL);
    usb_out_buffer = kmalloc(usb_max_size, GFP_KERNEL);

    //LDR_value = usb_read_serial(1);
    //printk("LDR Value: %d\n", LDR_value);

    // add led to /sys/class/leds/smartlamp_led
    led_cdev = devm_kzalloc(&interface->dev, sizeof(*led_cdev), GFP_KERNEL);
    if (!led_cdev) {
        printk(KERN_ERR "SmartLamp: Falha ao alocar memória para led_classdev\n");
        return -ENOMEM;
    }

    led_cdev->name = "smartlamp_led";
    led_cdev->brightness_set = led_set_brightness;
    led_cdev->brightness_get = led_get_brightness;

    // Registra o LED
    if (led_classdev_register(&interface->dev, led_cdev)) {
        printk(KERN_ERR "SmartLamp: Falha ao registrar led_classdev\n");
        return -EINVAL;
    }
    printk(KERN_INFO "SmartLamp: Dispositivo conectado com sucesso.\n");

    return 0;
}

// Executado quando o dispositivo USB é desconectado da USB
static void usb_disconnect(struct usb_interface *interface) {
    printk(KERN_INFO "SmartLamp: Dispositivo desconectado.\n");
    if (sys_obj) kobject_put(sys_obj);      // Remove os arquivos em /sys/kernel/smartlamp
    if (led_cdev) {
        led_classdev_unregister(led_cdev);  // Remove o LED de /sys/class/leds
        // devm_kfree não é necessário, pois devm_kzalloc será limpo automaticamente
        led_cdev = NULL;
    }
    kfree(usb_in_buffer);                   // Desaloca buffers
    kfree(usb_out_buffer);
}

// Envia um comando via USB, espera e retorna a resposta do dispositivo (convertido para int)
// Exemplo de Comando:  SET_LED 80
// Exemplo de Resposta: RES SET_LED 1
// Exemplo de chamada da função usb_send_cmd para SET_LED: usb_send_cmd("SET_LED", 80);
static int usb_send_cmd(char *cmd, int param) {
    int ret, actual_size;
    char resp_expected[MAX_RECV_LINE];      // Resposta esperada do comando

    printk(KERN_INFO "SmartLamp: Enviando comando: %s\n", cmd);

    // preencha o buffer                     // Caso contrário, é só o comando mesmo
    strcpy(usb_out_buffer, cmd);
    // Envia o comando (usb_out_buffer) para a USB
    // Procure a documentação da função usb_bulk_msg para entender os parâmetros
    ret = usb_bulk_msg(smartlamp_device, usb_sndbulkpipe(smartlamp_device, usb_out), usb_out_buffer, strlen(usb_out_buffer), &actual_size, 1000);
    if (ret) {
        printk(KERN_ERR "SmartLamp: Erro de codigo %d ao enviar comando!\n", ret);
        return -1;
    }

    sprintf(resp_expected, "RES %s", cmd);  // Resposta esperada. Ficará lendo linhas até receber essa resposta.

    return usb_read_serial(param);
}

static int usb_read_serial(int mode) {
    int ret, actual_size;
    int retries = 10;                       // Tenta algumas vezes receber uma resposta da USB. Depois desiste.
    long ldr_value; 
    char prefix[20];
    if (mode == 1)
        strcpy(prefix,"RES GET_LDR ");
    else if (mode == 2)
        strcpy(prefix,"RES GET_LED ");
    else if (mode == 3)
        strcpy(prefix,"RES SET_LED ");
    else if (mode == 4)
        strcpy(prefix,"RES GET_TEMP ");
    else if (mode == 5)
        strcpy(prefix,"RES GET_HUM ");

    const size_t prefix_len = strlen(prefix);

    // Espera pela resposta correta do dispositivo (desiste depois de várias tentativas)
    while (retries > 0) {
        // Lê os dados da porta serial e armazena em usb_in_buffer
            // usb_in_buffer - contem a resposta em string do dispositivo
            // actual_size - contem o tamanho da resposta em bytes
        char buffer[64];
        bool endloop = false;
        int bufferIdx = 0;
        while (!endloop) {
            ret = usb_bulk_msg(smartlamp_device, usb_rcvbulkpipe(smartlamp_device, usb_in), usb_in_buffer, min(usb_max_size, MAX_RECV_LINE), &actual_size, 1000);
            if (ret) {
                endloop = true;
                bufferIdx = 0;
                continue;
            }
            if (usb_in_buffer[0] != '\n')  {
                buffer[bufferIdx] = usb_in_buffer[0];
                bufferIdx++;
                continue;
            } else {
                buffer[bufferIdx] = '\0';
                endloop = true;
                continue;
            }
            bufferIdx = 0;
        }
        
        if (ret) {
            printk(KERN_ERR "SmartLamp: Erro ao ler dados da USB (tentativa %d). Codigo: %d\n", retries--, ret);
            continue;
        }
        printk("value %s\n", buffer);

        //usb_in_buffer[actual_size] = '\0';

        //caso tenha recebido a mensagem 'RES_LDR X' via serial acesse o buffer 'usb_in_buffer' e retorne apenas o valor da resposta X
        //retorne o valor de X em inteiro
         if (strncmp(buffer, prefix, prefix_len) == 0) {

            if (kstrtol(buffer + prefix_len, 10, &ldr_value) == 0) {
                // A conversão funcionou.
                printk(KERN_INFO "SmartLamp: Mensagem recebida: '%s', Valor LDR extraído: %ld\n", usb_in_buffer, ldr_value);
                return (int)ldr_value; // Retorna o valor como um inteiro
            } else {
                // A conversão falhou
                printk(KERN_WARNING "SmartLamp: Mensagem com prefixo correto, mas valor LDR inválido: '%s'\n", usb_in_buffer);
            }
        }

        // Se a mensagem não era a esperada, apenas ignora e tenta ler a próxima.
        retries--;
    }

    printk(KERN_ERR "SmartLamp: Nao foi possivel ler um valor LDR valido apos 10 tentativas.\n");
    return -1; 
}

// Executado quando o arquivo /sys/kernel/smartlamp/{led, ldr} é lido (e.g., cat /sys/kernel/smartlamp/led)
static ssize_t attr_show(struct kobject *sys_obj, struct kobj_attribute *attr, char *buff) {
    // value representa o valor do led ou ldr
    int value = -1;
    // attr_name representa o nome do arquivo que está sendo lido (ldr ou led)
    const char *attr_name = attr->attr.name;

    // printk indicando qual arquivo está sendo lido
    printk(KERN_INFO "SmartLamp: Lendo %s ...\n", attr_name);

    // Implemente a leitura do valor do led ou ldr usando a função usb_send_cmd()
    if (strcmp(attr_name,"ldr") == 0)
        value = usb_send_cmd("GET_LDR",1);
    else if (strcmp(attr_name,"led") == 0)
        value = usb_send_cmd("GET_LED",2);
    else if (strcmp(attr_name,"temp") == 0)
        value = usb_send_cmd("GET_TEMP",4);
    else if (strcmp(attr_name,"hum") == 0)
        value = usb_send_cmd("GET_HUM",5);
    sprintf(buff, "%d\n", value);                   // Cria a mensagem com o valor do led, ldr
    return strlen(buff);
}


// Essa função não deve ser alterada durante a task sysfs
// Executado quando o arquivo /sys/kernel/smartlamp/{led, ldr} é escrito (e.g., echo "100" | sudo tee -a /sys/kernel/smartlamp/led)
static ssize_t attr_store(struct kobject *sys_obj, struct kobj_attribute *attr, const char *buff, size_t count) {
    long ret, value;
    const char *attr_name = attr->attr.name;

    // Converte o valor recebido para long
    ret = kstrtol(buff, 10, &value);
    if (ret) {
        printk(KERN_ALERT "SmartLamp: valor de %s invalido.\n", attr_name);
        return -EACCES;
    }

    printk(KERN_INFO "SmartLamp: Setando %s para %ld ...\n", attr_name, value);

    // utilize a função usb_send_cmd para enviar o comando SET_LED X
    char cmd[12] = "SET_LED ";
    char number[4] = "";
    sprintf(number,"%ld",value);
    strcat(cmd,number);
    //char test[] = "SET_LED 100";
    ret = usb_send_cmd(cmd,3);

    if (ret < 0) {
        printk(KERN_ALERT "SmartLamp: erro ao setar o valor do %s.\n", attr_name);
        return -EACCES;
    }

    return strlen(buff);
}
