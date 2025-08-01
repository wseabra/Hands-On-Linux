
#include <linux/module.h>
#include <linux/usb.h>
#include <linux/slab.h>

MODULE_AUTHOR("DevTITANS <devtitans@icomp.ufam.edu.br>");
MODULE_DESCRIPTION("Driver de acesso ao SmartLamp (ESP32 com Chip Serial CP2102");
MODULE_LICENSE("GPL");


#define MAX_RECV_LINE 100 // Tamanho máximo de uma linha de resposta do dispositvo USB


static struct usb_device *smartlamp_device;        // Referência para o dispositivo USB
static uint usb_in, usb_out;                       // Endereços das portas de entrada e saida da USB
static char *usb_in_buffer, *usb_out_buffer;       // Buffers de entrada e saída da USB
static int usb_max_size;                           // Tamanho máximo de uma mensagem USB

#define VENDOR_ID  0x10c4  /* Encontre o VendorID  do smartlamp */
#define PRODUCT_ID  0xea60  /* Encontre o ProductID do smartlamp */
static const struct usb_device_id id_table[] = { { USB_DEVICE(VENDOR_ID, PRODUCT_ID) }, {} };

static int  usb_probe(struct usb_interface *ifce, const struct usb_device_id *id); // Executado quando o dispositivo é conectado na USB
static void usb_disconnect(struct usb_interface *ifce);                           // Executado quando o dispositivo USB é desconectado da USB
static int  usb_read_serial(void);   

// Executado quando o arquivo /sys/kernel/smartlamp/{led, ldr} é lido (e.g., cat /sys/kernel/smartlamp/led)
static ssize_t attr_show(struct kobject *sys_obj, struct kobj_attribute *attr, char *buff);
// Executado quando o arquivo /sys/kernel/smartlamp/{led, ldr} é escrito (e.g., echo "100" | sudo tee -a /sys/kernel/smartlamp/led)
static ssize_t attr_store(struct kobject *sys_obj, struct kobj_attribute *attr, const char *buff, size_t count);   

// Variáveis para criar os arquivos no /sys/kernel/smartlamp/{led, ldr}
static struct kobj_attribute  led_attribute = __ATTR(led, S_IRUGO | S_IWUSR, attr_show, attr_store);
static struct kobj_attribute  ldr_attribute = __ATTR(ldr, S_IRUGO | S_IWUSR, attr_show, attr_store);
static struct kobj_attribute  dht_attribute = __ATTR(dht, S_IRUGO | S_IWUSR, attr_show, attr_store);

static struct attribute      *attrs[]       = { &led_attribute.attr, &ldr_attribute.attr, &dht_attribute, NULL };
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

    LDR_value = usb_read_serial();

    printk("LDR Value: %d\n", LDR_value);

    return 0;
}

// Executado quando o dispositivo USB é desconectado da USB
static void usb_disconnect(struct usb_interface *interface) {
    printk(KERN_INFO "SmartLamp: Dispositivo desconectado.\n");
    if (sys_obj) kobject_put(sys_obj);      // Remove os arquivos em /sys/kernel/smartlamp
    kfree(usb_in_buffer);                   // Desaloca buffers
    kfree(usb_out_buffer);
}

static int usb_read_serial() {
    int ret, actual_size;
    int retries = 10;                       // Tenta algumas vezes receber uma resposta da USB. Depois desiste.
    long ldr_value; 

    const char *prefix = "RES GET_LDR ";
    const size_t prefix_len = strlen(prefix);

    // Espera pela resposta correta do dispositivo (desiste depois de várias tentativas)
    while (retries > 0) {
        // Lê os dados da porta serial e armazena em usb_in_buffer
            // usb_in_buffer - contem a resposta em string do dispositivo
            // actual_size - contem o tamanho da resposta em bytes
        char buffer[64];
        bool endloop = false;
        int bufferIdx = 0;
        while (!endloop)
        {
            ret = usb_bulk_msg(smartlamp_device, usb_rcvbulkpipe(smartlamp_device, usb_in), usb_in_buffer, min(usb_max_size, MAX_RECV_LINE), &actual_size, 5000);
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
        printk("LDR value %s\n", buffer);

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
    const char *attr_name = attr->attr.name;

    printk(KERN_INFO "SmartLamp: Lendo %s ...\n", attr_name);

    if (strcmp(attr_name, "ldr") == 0) {
        char cmd[] = "GET_LDR\n";
        int ret, actual_size;
        // Envia comando para o dispositivo
        ret = usb_bulk_msg(smartlamp_device, usb_sndbulkpipe(smartlamp_device, usb_out), cmd, strlen(cmd), &actual_size, 5000);
        if (ret) {
            printk(KERN_ERR "SmartLamp: Erro ao enviar comando GET_LDR. Codigo: %d\n", ret);
        } else {
            // Lê resposta
            value = usb_read_serial();
        }
    } else if (strcmp(attr_name, "led") == 0) {
        // Envia comando GET_LED para o dispositivo
        // Monta comando
        char cmd[] = "GET_LED\n";
        int ret, actual_size;
        // Envia comando para o dispositivo
        ret = usb_bulk_msg(smartlamp_device, usb_sndbulkpipe(smartlamp_device, usb_out), cmd, strlen(cmd), &actual_size, 5000);
        if (ret) {
            printk(KERN_ERR "SmartLamp: Erro ao enviar comando GET_LED. Codigo: %d\n", ret);
        } else {
            // Lê resposta
            value = usb_read_serial();
        }
    }

    sprintf(buff, "%d\n", value);
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

    if (strcmp(attr_name, "led") == 0) {
        // Monta comando SET_LED
        char cmd[32];
        int actual_size, send_ret;
        snprintf(cmd, sizeof(cmd), "SET_LED %ld", value);
        send_ret = usb_bulk_msg(smartlamp_device, usb_sndbulkpipe(smartlamp_device, usb_out), cmd, strlen(cmd), &actual_size, 5000);
        if (send_ret) {
            printk(KERN_ALERT "SmartLamp: erro ao enviar comando SET_LED. Codigo: %d\n", send_ret);
            return -EACCES;
        }
    }

    return strlen(buff);
}
