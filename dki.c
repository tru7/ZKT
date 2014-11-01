/*****************************************************************
**
**	@(#) dki.c  (c) Jan 2005  Holger Zuleger  hznet.de
**
**	See LICENCE file for licence
**
*****************************************************************/

# include <stdio.h>
# include <string.h>
# include <stdlib.h>
# include <sys/types.h>
# include <sys/stat.h>
# include <dirent.h>
# include <assert.h>
# include "config.h"
# include "debug.h"
# include "misc.h"
# include "zconf.h"
#define	extern
# include "dki.h"
#undef	extern

/*****************************************************************
**	private (static) function declaration and definition
*****************************************************************/
static	char	dki_estr[255+1];

static	dki_t	*dki_alloc ()
{
	dki_t	*dkp = malloc (sizeof (dki_t));

	if ( (dkp = malloc (sizeof (dki_t))) )
	{
		memset (dkp, 0, sizeof (dki_t));
		return dkp;
	}

	snprintf (dki_estr, sizeof (dki_estr),
			"dki_alloc: Out of memory");
	return NULL;
}

static	int	dki_readfile (FILE *fp, dki_t *dkp)
{
	int	algo,	flags,	type;
	int	ret;
	char	*p;
	char	buf[4095+1];

	assert (dkp != NULL);
	assert (fp != NULL);

	if ( fscanf (fp, "%4095s", buf) != 1 )
		return -1;

	if ( strcmp (buf, dkp->name) != 0 )
		return -2;

	if ( (ret = fscanf (fp, " IN DNSKEY %d %d %d", &flags, &type, &algo)) != 3 &&
	     (ret = fscanf (fp, "KEY %d %d %d", &flags, &type, &algo)) != 3 )
		return -3;
	if ( type != 3 || algo != dkp->algo )
		return -4;		/* no DNSKEY or algorithm mismatch */
	if ( ((flags >> 8) & 0xFF) != 01 )
		return -5;		/* no ZONE key */
	dkp->flags = flags;

	if ( fgets (buf, sizeof buf, fp) == NULL || buf[0] == '\0' )
		return -6;
	p = buf + strlen (buf);
	*--p = '\0';		/* delete trailing \n */
	/* delete leading ws */
	for ( p = buf; *p  && isspace (*p); p++ )
		;

	dkp->pubkey = strdup (p);

	return 0;
}

/*****************************************************************
**	public function definition
*****************************************************************/

/*****************************************************************
**	dki_free ()
*****************************************************************/
void	dki_free (dki_t *dkp)
{
	assert (dkp != NULL);

	if ( dkp->pubkey )
		free (dkp->pubkey);
	free (dkp);
}

/*****************************************************************
**	dki_freelist ()
*****************************************************************/
void	dki_freelist (dki_t **listp)
{
	dki_t	*curr;
	dki_t	*next;

	assert (listp != NULL);

	curr = *listp;
	while ( curr )
	{
		next = curr->next;
		dki_free (curr);
		curr = next;
	}
	if ( *listp )
		*listp = NULL;
}

/*****************************************************************
**	dki_new ()
**	create new keyfile
**	allocate memory for new dki key and init with keyfile
*****************************************************************/
dki_t	*dki_new (const char *dir, const char *name, int ksk, int algo, int bitsize)
{
	char	cmdline[254+1];
	char	fname[254+1];
	FILE	*fp;
	int	len;
	char	*flag = "";

	if ( ksk )
		flag = "-f KSK";

	/* TODO: check if "name" and "dir" is safe (strings coming from external source) ! */
	if ( dir && *dir )
		snprintf (cmdline, sizeof (cmdline), "cd %s ; %s -n ZONE -a %s -b %d %s %s",
			dir, KEYGENCMD, dki_algo2str(algo), bitsize, flag, name);
	else
		snprintf (cmdline, sizeof (cmdline), "%s -n ZONE -a %s -b %d %s %s",
			KEYGENCMD, dki_algo2str(algo), bitsize, flag, name);

	if ( (fp = popen (cmdline, "r")) == NULL || fgets (fname, sizeof fname, fp) == NULL )
		return NULL;
	pclose (fp);

	len = strlen (fname) - 1;
	if ( len >= 0 && fname[len] == '\n' )
		fname[len] = '\0';

	return dki_read (dir, fname);
}

/*****************************************************************
**	dki_read ()
**	read key from file 'filename' (with or without extension .key or .private)
*****************************************************************/
dki_t	*dki_read (const char *dirname, const char *filename)
{
	dki_t	*dkp;
	FILE	*fp;
	struct	stat	st;
	int	len;
	int	err;
	char	fname[MAX_FNAMESIZE+1];
	char	path[MAX_PATHSIZE+1];

	if ( (dkp = dki_alloc ()) == NULL )
		return (NULL);

	len = sizeof (fname) - 1;
	fname[len] = '\0';
	strncpy (fname, filename, len);

	len = strlen (fname);			/* delete extension */
	if ( len > 4 && strcmp (&fname[len - 4], ".key") == 0 )
		fname[len - 4] = '\0';
	else if ( len > 10 && strcmp (&fname[len - 10], DKI_PUB_FILEEXT) == 0 )
		fname[len - 10] = '\0';
	else if ( len > 8 && strcmp (&fname[len - 8], DKI_ACT_FILEEXT) == 0 )
		fname[len - 8] = '\0';
	else if ( len > 12 && strcmp (&fname[len - 12], DKI_DEP_FILEEXT) == 0 )
		fname[len - 12] = '\0';
	dbg_line ();

	assert (strlen (dirname)+1 < sizeof (dkp->dname));
	strcpy (dkp->dname, dirname);

	assert (strlen (fname)+1 < sizeof (dkp->fname));
	strcpy (dkp->fname, fname);
	dbg_line ();
	if ( sscanf (fname, "K%254[^+]+%hd+%d", dkp->name, &dkp->algo, &dkp->tag) != 3 )
	{
		snprintf (dki_estr, sizeof (dki_estr),
			"dki_read: Filename didn't match expected format (%s)", fname);
		return (NULL);
	}

	pathname (path, sizeof (path), dkp->dname, dkp->fname, ".key");
	dbg_val ("dki_read: path \"%s\"\n", path);
	if ( (fp = fopen (path, "r")) == NULL )
	{
		snprintf (dki_estr, sizeof (dki_estr),
			"dki_read: Can\'t open file \"%s\" for reading", path);
		return (NULL);
	}
	
	dbg_line ();
	if ( fstat (fileno(fp), &st) )
	{
		snprintf (dki_estr, sizeof (dki_estr),
			"dki_read: Can\'t stat file %s", fname);
		return (NULL);
	}

	dkp->time = st.st_mtime;
	dbg_line ();
	if ( (err = dki_readfile (fp, dkp)) != 0 )
	{
		snprintf (dki_estr, sizeof (dki_estr),
			"dki_read: Can\'t read key from file %s (errno %d)", path, err);
		fclose (fp);
		return (NULL);
	}

	dbg_line ();
	pathname (path, sizeof (path), dkp->dname, dkp->fname, DKI_ACT_FILEEXT);
	if ( fileexist (path) )
		dkp->status = 'a';
	else
	{
		pathname (path, sizeof (path), dkp->dname, dkp->fname, DKI_PUB_FILEEXT);
		if ( fileexist (path) )
			dkp->status = 'p';
		else
			dkp->status = 'd';
	}

	dbg_line ();
	fclose (fp);

	dbg_line ();
	return dkp;
}

/*****************************************************************
**	dki_readdir ()
**	read key files from directory 'dir' and, if recursive if
**	true, from all directorys below that.
*****************************************************************/
int	dki_readdir (const char *dir, dki_t **listp, int recursive)
{
	dki_t	*dkp;
	DIR	*dirp;
	struct  dirent  *dentp;
	char	path[MAX_PATHSIZE+1];

	dbg_val ("directory: opendir(%s)\n", dir);
	if ( (dirp = opendir (dir)) == NULL )
		return 0;

	while ( (dentp = readdir (dirp)) != NULL )
	{
		if ( is_dotfile (dentp->d_name) )
			continue;

		dbg_val ("directory: check %s\n", dentp->d_name);
		pathname (path, sizeof (path), dir, dentp->d_name, NULL);
		if ( is_directory (path) && recursive )
		{
			dbg_val ("directory: recursive %s\n", path);
			dki_readdir (path, listp, recursive);
		}
		else if ( is_keyfilename (dentp->d_name) )
			if ( (dkp = dki_read (dir, dentp->d_name)) )
				dki_add (listp, dkp);
	}
	closedir (dirp);
	return 1;
}


/*****************************************************************
**	dki_setstatus ()
**	set status of key 
**	(move to ".published", ".private" or ".depreciated")
*****************************************************************/
int	dki_setstatus (dki_t *dkp, int status)
{
	char	frompath[MAX_PATHSIZE+1];
	char	topath[MAX_PATHSIZE+1];
	time_t	totime;

	if ( dkp == NULL )
		return 0;

	status = tolower (status);
	switch ( dkp->status )
	{
	case 'a':
		if ( status == 'a' )
			return 1;
		pathname (frompath, sizeof (frompath), dkp->dname, dkp->fname, DKI_ACT_FILEEXT);
		break;
	case 'd':
		if ( status == 'd' )
			return 1;
		pathname (frompath, sizeof (frompath), dkp->dname, dkp->fname, DKI_DEP_FILEEXT);
		break;
	case 'p':
		if ( status == 'p' )
			return 1;
		pathname (frompath, sizeof (frompath), dkp->dname, dkp->fname, DKI_PUB_FILEEXT);
		break;
	default:
		/* TODO: set error code */
		return 0;
	}

	dbg_val ("dki_setstatus: \"%s\"\n", frompath);
	dbg_val ("dki_setstatus: to status \"%c\"\n", status);
	totime = 0L;
	topath[0] = '\0';
	switch ( status )
	{
	case 'a':
		totime = get_mtime (frompath);    /* set .key file to time of .private file */
		pathname (topath, sizeof (topath), dkp->dname, dkp->fname, DKI_ACT_FILEEXT);
		break;
	case 'd':
		pathname (topath, sizeof (topath), dkp->dname, dkp->fname, DKI_DEP_FILEEXT);
		break;
	case 'p':
		pathname (topath, sizeof (topath), dkp->dname, dkp->fname, DKI_PUB_FILEEXT);
		break;
	}
	if ( topath[0] )
	{
		dbg_val ("dki_setstatus: to  \"%s\"\n", topath);
		if ( link (frompath, topath) == 0 )
			unlink (frompath);
		dkp->status = status;
		if ( !totime )
			totime = time (NULL);	/* set .key file to current time */
		pathname (topath, sizeof (topath), dkp->dname, dkp->fname, ".key");
		touch (topath, totime);	/* store/restore time of status change */
		return 1;
	}

	return 0;
}

/*****************************************************************
**	dki_depreciate ()
**	mark key as depreciated 
**	(move ".private" file to ".depreciated")
*****************************************************************/
int	dki_depreciate (dki_t *dkp)
{
	char	frompath[MAX_PATHSIZE+1];
	char	topath[MAX_PATHSIZE+1];
	time_t	totime;

	if ( dkp == NULL )
		return 0;

	topath[0] = '\0';
	if ( dkp->status == 'a' )
	{
		pathname (frompath, sizeof (frompath), dkp->dname, dkp->fname, DKI_ACT_FILEEXT);
		dbg_val ("dki_depreciate: \"%s\"\n", frompath);
		pathname (topath, sizeof (topath), dkp->dname, dkp->fname, DKI_DEP_FILEEXT);

		if ( link (frompath, topath) == 0 )
			unlink (frompath);
		dkp->status = 'd';
		totime = time (NULL);	/* set .key file to local time */
		pathname (topath, sizeof (topath), dkp->dname, dkp->fname, ".key");
		touch (topath, totime);	/* store/restore time of status change */
		return 1;
	}

	return 0;
}

/*****************************************************************
**	dki_activate ()
**	mark key as active
**	(move ".published" file to ".private")
*****************************************************************/
int	dki_activate (dki_t *dkp)
{
	char	frompath[MAX_PATHSIZE+1];
	char	topath[MAX_PATHSIZE+1];
	time_t	totime;

	if ( dkp == NULL )
		return 0;

	topath[0] = '\0';
	if ( dkp->status == 'p' )
	{
		pathname (frompath, sizeof (frompath), dkp->dname, dkp->fname, DKI_PUB_FILEEXT);
		dbg_val ("dki_activate: \"%s\"\n", frompath);
		pathname (topath, sizeof (topath), dkp->dname, dkp->fname, DKI_ACT_FILEEXT);

		if ( link (frompath, topath) == 0 )
			unlink (frompath);
		dkp->status = 'a';
		totime = time (NULL);	/* set .key file to local time */
		pathname (topath, sizeof (topath), dkp->dname, dkp->fname, ".key");
		touch (topath, totime);	/* store/restore time of status change */
		return 1;
	}

	return 0;
}

/*****************************************************************
**	dki_remove ()
**	delete files associated with key and free allocated memory
*****************************************************************/
dki_t	*dki_remove (dki_t *dkp)
{
	char	path[MAX_PATHSIZE+1];
	dki_t	*next;

	if ( dkp == NULL )
		return;

	pathname (path, sizeof (path), dkp->dname, dkp->fname, ".key");
	dbg_val ("dki_remove: %s \n", path);
	unlink (path);

	pathname (path, sizeof (path), dkp->dname, dkp->fname, DKI_PUB_FILEEXT);
	dbg_val ("dki_remove: %s \n", path);
	unlink (path);

	pathname (path, sizeof (path), dkp->dname, dkp->fname, DKI_ACT_FILEEXT);
	dbg_val ("dki_remove: %s \n", path);
	unlink (path);

	pathname (path, sizeof (path), dkp->dname, dkp->fname, DKI_DEP_FILEEXT);
	dbg_val ("dki_remove: %s \n", path);
	unlink (path);

	next = dkp->next;
	dki_free (dkp);

	return next;
}

/*****************************************************************
**	dki_algo2str ()
**	return string describing key algorithm
*****************************************************************/
char	*dki_algo2str (int algo)
{
	switch ( algo )
	{
	case DK_ALGO_RSA:	return ("RSAMD5");
	case DK_ALGO_DH:	return ("DH");
	case DK_ALGO_DSA:	return ("DSA");
	case DK_ALGO_EC:	return ("EC");
	case DK_ALGO_RSASHA1:	return ("RSASHA1");
	}
	return ("unknown");
}

/*****************************************************************
**	dki_geterrstr ()
**	return error string 
*****************************************************************/
const	char	*dki_geterrstr ()
{
	return dki_estr;
}

/*****************************************************************
**	dki_prt_dnskey ()
*****************************************************************/
int	dki_prt_dnskey (const dki_t *dkp, FILE *fp)
{
	char	*p;

	if ( dkp == NULL )
		return 0;
	fprintf (fp, "%s IN DNSKEY  ", dkp->name);
	fprintf (fp, "%d 3 %d (", dkp->flags, dkp->algo);
	fprintf (fp, "\n\t\t\t"); 
	for ( p = dkp->pubkey; *p ; p++ )
		if ( *p == ' ' )
			fprintf (fp, "\n\t\t\t"); 
		else
			putc (*p, fp);
	fprintf (fp, "\n\t\t) ; key id = %u\n", dkp->tag); 
}

/*****************************************************************
**	dki_prt_comment ()
*****************************************************************/
int	dki_prt_comment (const dki_t *dkp, FILE *fp)
{
	if ( dkp == NULL )
		return 0;
	fprintf (fp, "; %s  ", dkp->name);
	fprintf (fp, "tag=%d  ", dkp->tag);
	fprintf (fp, "algo=%s  ", dki_algo2str(dkp->algo));
	fprintf (fp, "generated %s\n", time2str (dkp->time)); 
}

/*****************************************************************
**	dki_prt_trustedkey ()
*****************************************************************/
int	dki_prt_trustedkey (const dki_t *dkp, FILE *fp)
{
	char	*p;
	int	spaces;

	if ( dkp == NULL )
		return 0;
	fprintf (fp, "\"%s\"  ", dkp->name);
	spaces = 22 - (strlen (dkp->name) + 3);
	fprintf (fp, "%*s", spaces > 0 ? spaces : 0 , " ");
	fprintf (fp, "%d 3 %d ", dkp->flags, dkp->algo);
	if ( spaces < 0 )
		fprintf (fp, "\n\t\t\t%7s", " "); 
	fprintf (fp, "\"");
	for ( p = dkp->pubkey; *p ; p++ )
		if ( *p == ' ' )
			fprintf (fp, "\n\t\t\t\t"); 
		else
			putc (*p, fp);
	fprintf (fp, "\" ; # key id = %d\n\n", dkp->tag); 
}


/*****************************************************************
**	dki_cmp () 	return <0 | 0 | >0
*****************************************************************/
int	dki_cmp (const dki_t *a, const dki_t *b, short merge_keytype)
{
	int	res;

	if ( a == NULL )
		return -1;
	if ( b == NULL )
		return 1;

	/* sort by domain name */
	if ( (res = domaincmp (a->name, b->name)) != 0 )
		return res; 
	if ( !merge_keytype )		/* then by key type */
		if ( (res = dki_isksk (b) - dki_isksk (a)) != 0 )
			return res;

	return dki_timecmp (a, b);	/* last by creation time */
}

/*****************************************************************
**	dki_timecmp ()
*****************************************************************/
int	dki_timecmp (const dki_t *a, const dki_t *b)
{
	if ( a == NULL )
		return -1;
	if ( b == NULL )
		return 1;
	return ((ulong)a->time - (ulong)b->time);
}

/*****************************************************************
**	dki_time ()
*****************************************************************/
time_t	dki_time (const dki_t *dkp)
{
	assert (dkp != NULL);
	return (dkp->time);
}

/*****************************************************************
**	dki_age ()
*****************************************************************/
int	dki_age (const dki_t *dkp, time_t curr)
{
	assert (dkp != NULL);
	return ((ulong)curr - (ulong)dkp->time);
}

/*****************************************************************
**	dki_isksk ()
*****************************************************************/
int	dki_isksk (const dki_t *dkp)
{
	assert (dkp != NULL);
	return (dkp->flags & DK_FLAG_KSK) == DK_FLAG_KSK;
}

/*****************************************************************
**	dki_isdepreciated ()
*****************************************************************/
int	dki_isdepreciated (const dki_t *dkp)
{
	assert (dkp != NULL);
	return (dkp->status == 'd');
}

/*****************************************************************
**	dki_status ()
*****************************************************************/
int	dki_status (const dki_t *dkp)
{
	assert (dkp != NULL);
	return (dkp->status);
}

/*****************************************************************
**	dki_statusstr ()
*****************************************************************/
const	char	*dki_statusstr (const dki_t *dkp)
{
	assert (dkp != NULL);
	switch ( dkp->status )
	{
	case DKI_ACT:	return "active";
	case DKI_PUB:   return "prepublished";
	case DKI_DEP:   return "depreciated";
	}
	return "unknown";
}

/*****************************************************************
**	dki_add ()
*****************************************************************/
dki_t	*dki_add (dki_t **list, dki_t *new)
{
	dki_t	*curr;
	dki_t	*last;

	if ( list == NULL )
		return NULL;
	if ( new == NULL )
		return *list;

	last = curr = *list;
	while ( curr && dki_cmp (curr, new, 0) < 0 )
	{
		last = curr;
		curr = curr->next;
	}

	if ( curr == *list )	/* add node at start of list */
		*list = new;
	else			/* add node at end or between two nodes */
		last->next = new;
	new->next = curr;
	
	return *list;
}

/*****************************************************************
**	dki_search ()
*****************************************************************/
const dki_t	*dki_search (const dki_t *list, int tag, const char *name)
{
	const dki_t	*curr;

	curr = list;
	if ( tag )
		while ( curr && (tag != curr->tag ||
				(name && *name && strcmp (name, curr->name) != 0)) )
			curr = curr->next;
	else if ( name && *name )
		while ( curr && strcmp (name, curr->name) != 0 )
			curr = curr->next;
	else
		curr = NULL;

	return curr;
}

/*****************************************************************
**	dki_find ()
*****************************************************************/
const dki_t	*dki_find (const dki_t *list, int ksk, int status, int no)
{
	const	dki_t	*dkp;
	const	dki_t	*last;

	last = NULL;
	for ( dkp = list; no > 0 && dkp; dkp = dkp->next )
		if ( dki_isksk (dkp) == ksk && dki_status (dkp) == status )
		{
			no--;
			last = dkp;
		}

	return last;
}
