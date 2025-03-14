#include <bio/net.h>
#include <string.h>

int
bio_net_address_compare(const bio_addr_t* lhs, const bio_addr_t* rhs) {
	if (lhs->type == rhs->type) {
		switch (lhs->type) {
			case BIO_ADDR_IPV4:
				return memcmp(lhs->addr.ipv4, rhs->addr.ipv4, sizeof(lhs->addr.ipv4));
			case BIO_ADDR_IPV6:
				return memcmp(lhs->addr.ipv6, rhs->addr.ipv6, sizeof(lhs->addr.ipv6));
			case BIO_ADDR_NAMED:
				{
					int lhs_len = (int)lhs->addr.named.len;
					int rhs_len = (int)rhs->addr.named.len;
					int min_len = lhs_len < rhs_len ? lhs_len : rhs_len;
					int cmp = memcmp(lhs->addr.named.name, rhs->addr.named.name, min_len);
					if (cmp != 0) {
						return cmp;
					} else {
						return lhs_len - rhs_len;
					}
				}
		}
	} else {
		return (int)lhs->type - (int)rhs->type;
	}
}
